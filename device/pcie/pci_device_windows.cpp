// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Windows implementation of pci_device.cpp, backed by the tt-wind kernel-mode driver through the
// tt-kmd-lib shim. Follows the Linux implementation where the driver supports it and keeps the
// rest "not yet supported on Windows":
//
//  - Devices are identified by ordinal index into the sorted device-interface list (stable
//    ordering); the ordinal doubles as the chardev path handed to tt_device_open().
//  - The Linux code maps the whole of BAR0/BAR2 via mmap() offsets from QUERY_MAPPINGS. tt-wind
//    caps a single user mapping at 8 MiB, so only the slices UMD actually uses are mapped: the
//    3 MiB BAR0 slice at offset 509 MiB (NOC2AXI + ARC CSM, served through PCIDevice::bar0) and
//    BAR2 (iATU/DMA registers, clamped to the 8 MiB cap). The TLB configuration register page is
//    not mapped: TLB configuration goes through the driver's CONFIGURE_TLB ioctl instead of
//    direct BAR0 register writes (see configure_tlb below).
//  - DMA (pin pages / DMA buffers), reset, power state and sysfs-derived attributes are not
//    supported by the driver yet; those entry points throw. numa_node is -1 and revision 0.

#include "umd/device/pcie/pci_device.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "common/utils.hpp"
#include "tt-kmd-lib/pci_ids.h"
#include "tt-kmd-lib/tt_kmd_lib.h"
#include "tt-kmd-lib/tt_kmd_lib_windows.h"
#include "umd/device/pcie/silicon_tlb_handle.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {

[[noreturn]] void throw_not_supported(const char *what) {
    UMD_THROW(error::RuntimeError, std::string(what) + " is not yet supported on Windows.");
}

// The driver only implements Blackhole's 2 MiB TLB windows; their ids are [0, 202).
constexpr uint32_t BLACKHOLE_NUM_2M_TLBS = 202;
constexpr size_t TLB_2M_WINDOW_SIZE = 1ULL << 21;

}  // namespace

tt::ARCH PciDeviceInfo::get_arch() const {
    if (this->device_id == TT_WORMHOLE_PCI_DEVICE_ID) {
        return tt::ARCH::WORMHOLE_B0;
    } else if (this->device_id == TT_BLACKHOLE_PCI_DEVICE_ID) {
        return tt::ARCH::BLACKHOLE;
    }
    return tt::ARCH::Invalid;
}

std::vector<int> PCIDevice::get_all_device_ids() {
    int count = tt_windows_device_count();
    if (count < 0) {
        log_warning(LogUMD, "Enumerating Tenstorrent devices failed: {}", strerror(-count));
        return {};
    }

    std::vector<int> device_ids;
    device_ids.reserve(count);
    for (int i = 0; i < count; i++) {
        device_ids.push_back(i);
    }
    return device_ids;
}

// Mirrors the Linux enumerate_devices(): honor TT_VISIBLE_DEVICES by logical id or BDF pattern.
// Ordinals are already BDF-stable (sorted interface-path order), so no extra sort is needed.
std::vector<int> PCIDevice::enumerate_devices() {
    const char *tt_visible_devices_env = std::getenv("TT_VISIBLE_DEVICES");
    if (!tt_visible_devices_env) {
        return get_all_device_ids();
    }

    std::string tt_visible_devices_str(tt_visible_devices_env);
    if (tt_visible_devices_str.empty()) {
        return {};
    }

    std::vector<std::string> device_tokens = utils::split_string_by_comma(tt_visible_devices_str);

    std::map<std::string, int> bdf_to_device_id_map = get_bdf_to_device_id_map();
    std::vector<int> all_device_ids = get_all_device_ids();

    std::set<int> filtered_device_ids;

    for (const auto &device_token : device_tokens) {
        // Check if token is BDF format (contains colon and dot).
        if (utils::is_bdf_string(device_token)) {
            bool matched_bdf_pattern = false;
            for (const auto &[bdf, device_id] : bdf_to_device_id_map) {
                if (bdf.find(device_token) != std::string::npos) {
                    filtered_device_ids.insert(device_id);
                    log_debug(
                        LogUMD, "Added device ID {} with BDF {} because of pattern: {}", device_id, bdf, device_token);
                    matched_bdf_pattern = true;
                }
            }

            if (!matched_bdf_pattern) {
                UMD_THROW(
                    error::RuntimeError,
                    fmt::format("BDF pattern in TT_VISIBLE_DEVICES: {} did not match any devices.", device_token));
            }

            continue;
        }

        if (utils::is_integer_string(device_token)) {
            int logical_device_id = std::stoi(device_token);

            if (logical_device_id < 0 || logical_device_id >= static_cast<int>(all_device_ids.size())) {
                UMD_THROW(
                    error::RuntimeError,
                    fmt::format(
                        "Invalid device ID in TT_VISIBLE_DEVICES: {}.  Valid device identifiers are either integers or "
                        "part of the BDF string. Valid integer IDs are between 0 and {}.",
                        device_token,
                        all_device_ids.size() - 1));
            }

            filtered_device_ids.insert(all_device_ids[logical_device_id]);
        } else {
            UMD_THROW(
                error::RuntimeError,
                fmt::format(
                    "Invalid device identifier in TT_VISIBLE_DEVICES: {}.  Valid device identifiers are either "
                    "integers or "
                    "part of the BDF string.",
                    device_token));
        }
    }

    return std::vector<int>(filtered_device_ids.begin(), filtered_device_ids.end());
}

// Ordinals are already assigned in sorted interface-path order, which is stable; there is no
// separate BDF ordering to apply.
std::vector<int> PCIDevice::sort_ids_based_on_bdf(const std::vector<int> &pci_device_ids) { return pci_device_ids; }

std::map<int, PciDeviceInfo> PCIDevice::enumerate_devices_info() {
    std::map<int, PciDeviceInfo> infos;
    for (int n : PCIDevice::enumerate_devices()) {
        try {
            infos[n] = read_device_info(std::to_string(n));
        } catch (...) {
        }
    }
    return infos;
}

std::optional<int> PCIDevice::get_pci_device_id(int umd_logical_id) {
    std::vector<int> enumerated_ids = PCIDevice::enumerate_devices();
    if (umd_logical_id < 0 || umd_logical_id >= static_cast<int>(enumerated_ids.size())) {
        return std::nullopt;
    }
    return enumerated_ids[umd_logical_id];
}

std::map<std::string, int> PCIDevice::get_bdf_to_device_id_map() {
    std::map<std::string, int> bdf_to_device_id;
    for (int device_id : get_all_device_ids()) {
        try {
            PciDeviceInfo device_info = read_device_info(std::to_string(device_id));
            bdf_to_device_id[device_info.pci_bdf] = device_id;
        } catch (...) {
        }
    }
    return bdf_to_device_id;
}

PciDeviceInfo PCIDevice::read_device_info(const std::string &device_path) {
    tt_device_t *dev_handle = nullptr;
    int err = tt_device_open(device_path.c_str(), &dev_handle, 0);
    if (err != 0) {
        UMD_THROW(error::RuntimeError, fmt::format("Failed to open device {}: {}", device_path, strerror(-err)));
    }

    tt_device_attrs_t attrs{};
    err = tt_device_get_attrs(dev_handle, &attrs);
    tt_device_close(dev_handle);
    if (err != 0) {
        UMD_THROW(
            error::RuntimeError, fmt::format("Failed to read device info for {}: {}", device_path, strerror(-err)));
    }

    const auto bus = static_cast<uint16_t>(attrs.pci_bus);
    const auto dev = static_cast<uint16_t>(attrs.pci_device);
    const auto fn = static_cast<uint16_t>(attrs.pci_function);

    std::string pci_bdf = fmt::format("{:04x}:{:02x}:{:02x}.{:x}", attrs.pci_domain, bus, dev, fn);

    return PciDeviceInfo{
        static_cast<uint16_t>(attrs.pci_vendor_id),
        static_cast<uint16_t>(attrs.pci_device_id),
        static_cast<uint16_t>(attrs.pci_subsystem_vendor_id),
        static_cast<uint16_t>(attrs.pci_subsystem_id),
        static_cast<uint16_t>(attrs.pci_domain),
        bus,
        dev,
        fn,
        pci_bdf,
        std::nullopt /* physical_slot: not exposed on Windows */};
}

// tt-wind does not put the host behind an IOMMU on the device's behalf; DMA remapping status is
// not queryable from user mode here yet.
bool PCIDevice::detect_iommu(const PciDeviceInfo & /*device_info*/) { return false; }

PCIDevice::PCIDevice(int pci_device_number) :
    device_path(std::to_string(pci_device_number)),
    pci_device_num(pci_device_number),
    pci_device_file_desc(-1),  // no POSIX fd on Windows; all access goes through tt_device_handle
    info(read_device_info(device_path)),
    numa_node(-1),  // NUMA affinity not exposed by the driver yet
    revision(0),    // PCI revision not exposed by the driver yet
    arch(info.get_arch()),
    kmd_version(read_kmd_version()),
    iommu_enabled(false) {
    bar2_uc_size = 0;

    int ret_code = tt_device_open(device_path.c_str(), &tt_device_handle, 0);
    if (ret_code != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "tt_device_open failed with error code {} for PCI device with device ID {}.",
                ret_code,
                pci_device_number));
    }

    log_debug(LogUMD, "Opened PCI device {} (tt-wind backend)", pci_device_num);

    tt_bar_mappings_t bar_mappings{};
    int mappings_ret = tt_device_query_bar_mappings(tt_device_handle, &bar_mappings);
    if (mappings_ret != 0) {
        tt_device_close(tt_device_handle);
        tt_device_handle = nullptr;
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Query mappings failed on device {}: {}", pci_device_num, strerror(-mappings_ret)));
    }

    if (bar_mappings.resource0_uc.id != TT_BAR_MAPPING_RESOURCE0_UC ||
        bar_mappings.resource0_uc.size < bar0_mapping_offset + bar0_size) {
        tt_device_close(tt_device_handle);
        tt_device_handle = nullptr;
        UMD_THROW(error::RuntimeError, fmt::format("Device {} has no usable BAR0.", pci_device_num));
    }

    // 3 MiB UC slice of BAR0 at offset 509 MiB: NOC2AXI access + ARC CSM. Same region the Linux
    // implementation maps; well under the driver's 8 MiB per-mapping cap.
    int ret = tt_windows_map_bar(
        tt_device_handle, 0, TT_MMIO_CACHE_MODE_UC, bar0_mapping_offset, bar0_size, &bar0);
    if (ret != 0) {
        tt_device_close(tt_device_handle);
        tt_device_handle = nullptr;
        UMD_THROW(
            error::RuntimeError, fmt::format("BAR0 mapping failed for device {}: {}", pci_device_num, strerror(-ret)));
    }

    // BAR2 (resource1) UC: iATU and DMA-engine registers on Blackhole. Optional -- consumers
    // tolerate a null bar2_uc -- and clamped to the driver's per-mapping cap.
    if (arch == tt::ARCH::BLACKHOLE && bar_mappings.resource1_uc.id == TT_BAR_MAPPING_RESOURCE1_UC) {
        constexpr uint64_t max_map_bytes = 8ULL * 1024 * 1024;
        const uint64_t bar2_map_size = std::min<uint64_t>(bar_mappings.resource1_uc.size, max_map_bytes);
        ret = tt_windows_map_bar(tt_device_handle, 2, TT_MMIO_CACHE_MODE_UC, 0, bar2_map_size, &bar2_uc);
        if (ret != 0) {
            log_warning(
                LogUMD, "BAR2 UC mapping failed for device {}: {}", pci_device_num, strerror(-ret));
            bar2_uc = nullptr;
        } else {
            bar2_uc_size = bar2_map_size;
        }
    }

    // No PCIe DMA buffer: the driver does not support DMA-buffer allocation or page pinning yet.
    // dma_buffer stays empty; DMA paths check for that and fall back or throw.
}

PCIDevice::~PCIDevice() {
    if (bar0 != nullptr) {
        tt_windows_unmap_bar(tt_device_handle, bar0);
    }
    if (bar2_uc != nullptr) {
        tt_windows_unmap_bar(tt_device_handle, bar2_uc);
    }

    if (tt_device_handle != nullptr) {
        int ret_code = tt_device_close(tt_device_handle);
        if (ret_code != 0) {
            log_warning(
                LogUMD,
                "tt_device_close failed with error code {} for PCI device with device ID {}.",
                ret_code,
                pci_device_num);
        }
    }
}

uint64_t PCIDevice::map_for_hugepage(void * /*buffer*/, size_t /*size*/) {
    throw_not_supported("PCIDevice::map_for_hugepage");
}

std::pair<uint64_t, uint64_t> PCIDevice::map_buffer_to_noc(
    void * /*buffer*/, size_t /*size*/, DeviceBufferAccess /*device_access*/) {
    throw_not_supported("PCIDevice::map_buffer_to_noc");
}

std::pair<uint64_t, uint64_t> PCIDevice::map_hugepage_to_noc(void * /*hugepage*/, size_t /*size*/) {
    throw_not_supported("PCIDevice::map_hugepage_to_noc");
}

uint64_t PCIDevice::map_for_dma(void * /*buffer*/, size_t /*size*/, DeviceBufferAccess /*device_access*/) {
    throw_not_supported("PCIDevice::map_for_dma");
}

bool PCIDevice::is_read_only_page_pinning_supported() const { return false; }

void PCIDevice::unmap_for_dma(void * /*buffer*/, size_t /*size*/) { throw_not_supported("PCIDevice::unmap_for_dma"); }

// tt-wind does not report a version yet; 0.0.0 keeps every KMD-version-gated optional feature
// disabled without failing construction.
SemVer PCIDevice::read_kmd_version() { return SemVer{0, 0, 0}; }

SemVer PCIDevice::read_kernel_version() { return SemVer{0, 0, 0}; }

std::unique_ptr<TlbHandle> PCIDevice::allocate_tlb(
    const size_t tlb_size, const TlbMapping tlb_mapping, const bool verify_config) {
    try {
        return std::make_unique<SiliconTlbHandle>(*this, tlb_size, tlb_mapping, verify_config);
    } catch (const std::exception &e) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Failed to allocate TLB window. Note that the resource might be exhausted by some other hung "
                "process. Error: {}",
                e.what()));
    }
}

// On Linux this writes the TLB configuration registers directly through a BAR0 mapping. On
// Windows the write is driver-mediated: the packed register fields are converted back into the
// driver's NOC configuration and submitted via the CONFIGURE_TLB ioctl. The window must have been
// allocated on this device's handle (SiliconTlbHandle guarantees that: tlb_index is the
// driver-assigned window id).
//
// tlb_config arrives with local_offset already divided by the window size (register encoding);
// only the driver-supported 2 MiB windows exist, so the NOC address is local_offset << 21.
void PCIDevice::configure_tlb(const uint32_t tlb_index, const tlb_data &tlb_config, const bool verify) {
    UMD_ASSERT(
        tlb_index < BLACKHOLE_NUM_2M_TLBS,
        error::RuntimeError,
        fmt::format("TLB index {} out of range: only 2 MiB TLB windows are supported on Windows.", tlb_index));

    tt_noc_addr_config_t noc_config{};
    noc_config.addr = tlb_config.local_offset * TLB_2M_WINDOW_SIZE;
    noc_config.x_end = static_cast<uint16_t>(tlb_config.x_end);
    noc_config.y_end = static_cast<uint16_t>(tlb_config.y_end);
    noc_config.x_start = static_cast<uint16_t>(tlb_config.x_start);
    noc_config.y_start = static_cast<uint16_t>(tlb_config.y_start);
    noc_config.noc = static_cast<uint8_t>(tlb_config.noc_sel);
    noc_config.mcast = static_cast<uint8_t>(tlb_config.mcast);
    noc_config.ordering = static_cast<uint8_t>(tlb_config.ordering);
    noc_config.static_vc = static_cast<uint8_t>(tlb_config.static_vc);

    int ret = tt_windows_configure_tlb(tt_device_handle, tlb_index, &noc_config);
    if (ret != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Configuring TLB index {} failed on device {}: {}", tlb_index, pci_device_num, strerror(-ret)));
    }

    // verify: the ioctl is synchronous and the driver performs the register writes in kernel mode
    // before returning; there is no user mapping of the configuration registers to read back
    // through, so the extra read-back is skipped.
    (void)verify;
}

uint8_t PCIDevice::read_command_byte(const int /*pci_device_num*/) {
    throw_not_supported("PCIDevice::read_command_byte");
}

void PCIDevice::send_reset_ioctl_to_devices(
    const std::unordered_set<int> & /*pci_target_devices*/, TenstorrentResetDevice /*flag*/, bool /*ignore_failures*/) {
    throw_not_supported("PCIDevice::send_reset_ioctl_to_devices");
}

tt::ARCH PCIDevice::get_pcie_arch() {
    static bool enumerated_devices = false;
    static tt::ARCH cached_arch = tt::ARCH::Invalid;
    if (!enumerated_devices) {
        auto devices = PCIDevice::enumerate_devices_info();
        if (devices.empty()) {
            return tt::ARCH::Invalid;
        }
        enumerated_devices = true;
        cached_arch = devices.begin()->second.get_arch();
    }
    return cached_arch;
}

bool PCIDevice::is_arch_agnostic_reset_supported() { return false; }

bool PCIDevice::is_tlb_dmabuf_export_supported() { return false; }

int PCIDevice::export_tlb_dmabuf(
    size_t /*window_size*/, const tlb_data & /*config*/, uint64_t /*offset*/, uint64_t /*size*/) {
    throw_not_supported("PCIDevice::export_tlb_dmabuf");
}

// The driver has no power-state interface yet; ignore quietly like old-KMD Linux does.
void PCIDevice::set_power_state(bool /*busy*/) {}

}  // namespace tt::umd
