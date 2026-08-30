// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Windows stub for pci_device.cpp. The Linux implementation talks to the Tenstorrent kernel-mode
// driver through /dev/tenstorrent chardevs, sysfs and mmap, none of which exist on Windows yet.
// Every entry point either reports no devices or throws a "not yet supported on Windows" error.

#include "umd/device/pcie/pci_device.hpp"

#include "tt-kmd-lib/pci_ids.h"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {
[[noreturn]] void throw_not_supported(const char *what) {
    UMD_THROW(error::RuntimeError, std::string(what) + " is not yet supported on Windows.");
}
}  // namespace

tt::ARCH PciDeviceInfo::get_arch() const {
    if (this->device_id == TT_WORMHOLE_PCI_DEVICE_ID) {
        return tt::ARCH::WORMHOLE_B0;
    } else if (this->device_id == TT_BLACKHOLE_PCI_DEVICE_ID) {
        return tt::ARCH::BLACKHOLE;
    }
    return tt::ARCH::Invalid;
}

std::vector<int> PCIDevice::enumerate_devices() {
    // No PCIe device enumeration on Windows yet: report an empty system rather than failing, so
    // callers that merely probe for devices keep working.
    return {};
}

std::map<int, PciDeviceInfo> PCIDevice::enumerate_devices_info() { return {}; }

std::optional<int> PCIDevice::get_pci_device_id(int /*umd_logical_id*/) { return std::nullopt; }

PciDeviceInfo PCIDevice::read_device_info(const std::string & /*device_path*/) {
    throw_not_supported("PCIDevice::read_device_info");
}

bool PCIDevice::detect_iommu(const PciDeviceInfo & /*device_info*/) { return false; }

PCIDevice::PCIDevice(int pci_device_number) :
    device_path(),
    pci_device_num(pci_device_number),
    pci_device_file_desc(-1),
    info{},
    numa_node(-1),
    revision(0),
    arch(tt::ARCH::Invalid),
    kmd_version(),
    iommu_enabled(false) {
    throw_not_supported("Opening a PCIe device");
}

PCIDevice::~PCIDevice() {}

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

SemVer PCIDevice::read_kmd_version() { throw_not_supported("PCIDevice::read_kmd_version"); }

SemVer PCIDevice::read_kernel_version() { throw_not_supported("PCIDevice::read_kernel_version"); }

std::unique_ptr<TlbHandle> PCIDevice::allocate_tlb(
    const size_t /*tlb_size*/, const TlbMapping /*tlb_mapping*/, const bool /*verify_config*/) {
    throw_not_supported("PCIDevice::allocate_tlb");
}

void PCIDevice::configure_tlb(const uint32_t /*tlb_index*/, const tlb_data & /*tlb_config*/, const bool /*verify*/) {
    throw_not_supported("PCIDevice::configure_tlb");
}

uint8_t PCIDevice::read_command_byte(const int /*pci_device_num*/) {
    throw_not_supported("PCIDevice::read_command_byte");
}

void PCIDevice::send_reset_ioctl_to_devices(
    const std::unordered_set<int> & /*pci_target_devices*/, TenstorrentResetDevice /*flag*/, bool /*ignore_failures*/) {
    throw_not_supported("PCIDevice::send_reset_ioctl_to_devices");
}

tt::ARCH PCIDevice::get_pcie_arch() {
    // Mirrors the Linux behavior when no devices are present.
    return tt::ARCH::Invalid;
}

bool PCIDevice::is_arch_agnostic_reset_supported() { return false; }

bool PCIDevice::is_tlb_dmabuf_export_supported() { return false; }

int PCIDevice::export_tlb_dmabuf(
    size_t /*window_size*/, const tlb_data & /*config*/, uint64_t /*offset*/, uint64_t /*size*/) {
    throw_not_supported("PCIDevice::export_tlb_dmabuf");
}

void PCIDevice::set_power_state(bool /*busy*/) { throw_not_supported("PCIDevice::set_power_state"); }

std::vector<int> PCIDevice::get_all_device_ids() { return {}; }

std::vector<int> PCIDevice::sort_ids_based_on_bdf(const std::vector<int> &pci_device_ids) { return pci_device_ids; }

std::map<std::string, int> PCIDevice::get_bdf_to_device_id_map() { return {}; }

}  // namespace tt::umd
