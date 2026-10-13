/*
 * SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Windows implementation of silicon_sysmem_manager.cpp, backed by the tt-wind kernel-mode driver.
//
// The layout mirrors the Linux IOMMU path (init_iommu/pin_or_map_iommu): ONE contiguous region
// carved into fake per-channel HugepageMappings. The division of labor is different, though:
// tt-wind allocates the physically contiguous sysmem buffer at device start and arms outbound
// iATU region 0 itself (re-arming after reset), so there is nothing for user mode to pin or map
// to the device. UMD only queries the buffer (QUERY_SYSMEM), maps it into the process
// (MAP_SYSMEM: a single cached section view), and slices it into channels.
//
// When the driver reports sysmem unavailable (TotalSize == 0: the boot-time contiguous
// allocation, iATU arming, or loopback verification failed), this behaves like Linux with no
// hugepages installed: init logs an actionable warning and leaves no channels, so callers see
// get_num_host_mem_channels() == 0 and sysmem accesses fail with a readable error.

#include "umd/device/chip_helpers/silicon_sysmem_manager.hpp"

#include <fmt/format.h>

#include <cerrno>
#include <cstring>
#include <tt-logger/tt-logger.hpp>

#include "hugepage.hpp"  // MAX_HOST_MEM_CHANNELS
#include "tt-kmd-lib/tt_kmd_lib.h"
#include "tt-kmd-lib/tt_kmd_lib_windows.h"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

SiliconSysmemManager::SiliconSysmemManager(TTDevice *tt_device, uint32_t num_host_mem_channels) {
    tt_device_ = tt_device;
    pci_device_ = tt_device->get_pci_device();
    UMD_ASSERT(pci_device_ != nullptr, error::RuntimeError, "PCI device not available in TTDevice.");
    pcie_base_ = get_pcie_base_for_arch(pci_device_->get_arch());
    UMD_ASSERT(
        num_host_mem_channels <= MAX_HOST_MEM_CHANNELS,
        error::RuntimeError,
        fmt::format(
            "Only {} host memory channels are supported per device, but {} requested.",
            MAX_HOST_MEM_CHANNELS,
            num_host_mem_channels));

    SiliconSysmemManager::init_sysmem(num_host_mem_channels);
}

SiliconSysmemManager::~SiliconSysmemManager() { SiliconSysmemManager::unpin_or_unmap_sysmem(); }

bool SiliconSysmemManager::init_sysmem(uint32_t num_host_mem_channels) {
    if (num_host_mem_channels == 0) {
        // No sysmem channels requested, so just skip initialization.
        return true;
    }

    tt_device_t *handle = pci_device_->get_tt_device_handle();
    tt_sysmem_info_t info{};
    int err = tt_windows_query_sysmem(handle, &info);
    if (err == -ENOSYS) {
        log_warning(
            LogUMD,
            "The installed tt-wind driver does not implement the sysmem interface (QUERY_SYSMEM); update the "
            "driver to use host memory channels. Proceeding without sysmem.");
        return false;
    }
    if (err != 0) {
        log_warning(LogUMD, "Querying sysmem failed: {}. Proceeding without sysmem.", strerror(-err));
        return false;
    }
    if (info.total_size == 0) {
        log_warning(
            LogUMD,
            "{} host memory channel(s) requested but the driver reports sysmem unavailable. This usually means "
            "the boot-time contiguous allocation or the iATU arming failed (run `ttwind-info sysmem` for the "
            "failing stage), or a device reset is in flight. Proceeding without sysmem.",
            num_host_mem_channels);
        return false;
    }

    // Channel-count resolution: the Linux backend counts installed hugepages (sysfs) and clamps
    // the request to what is available; here the driver carved the buffer into ChannelCount
    // channels at device start, so clamp to the driver-reported count. Callers that pass the
    // auto-detected value (Cluster: min(MAX_HOST_MEM_CHANNELS, chips per MMIO device)) thereby
    // resolve to the driver's channel count.
    if (num_host_mem_channels > info.channel_count) {
        log_warning(
            LogUMD,
            "Requested {} host memory channels but the driver provides only {}; clamping to {}.",
            num_host_mem_channels,
            info.channel_count,
            info.channel_count);
        num_host_mem_channels = info.channel_count;
    }

    // The driver reports where the buffer sits in the NOC address space; UMD (and its clients)
    // hard-code that base per architecture, so a disagreement means the driver and UMD would
    // address different memory. Fail loudly rather than corrupt transfers.
    const uint64_t expected_noc_base = get_pcie_base_for_arch(pci_device_->get_arch());
    if (info.noc_address != expected_noc_base) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Driver mapped sysmem at NOC address {:#x} but UMD expects {:#x} for this architecture. The "
                "installed tt-wind driver and this UMD build disagree on the sysmem NOC base; update whichever "
                "is older.",
                info.noc_address,
                expected_noc_base));
    }
    pcie_base_ = info.noc_address;

    const uint64_t mapping_size = static_cast<uint64_t>(num_host_mem_channels) * info.channel_size;
    UMD_ASSERT(
        mapping_size <= info.max_map_bytes,
        error::RuntimeError,
        fmt::format(
            "Sysmem region of {:#x} bytes exceeds the driver's per-mapping cap of {:#x} bytes.",
            mapping_size,
            static_cast<uint64_t>(info.max_map_bytes)));

    void *mapping = nullptr;
    err = tt_windows_map_sysmem(handle, 0, mapping_size, &mapping);
    if (err != 0) {
        log_warning(
            LogUMD,
            "Mapping {:#x} bytes of sysmem failed: {}. Proceeding without sysmem.",
            mapping_size,
            strerror(-err));
        return false;
    }

    // Reuse the base-class fields the Linux IOMMU path uses for its one-region mapping; the
    // per-channel entries below are fake slices of it, exactly like init_iommu().
    iommu_mapping = mapping;
    iommu_mapping_size = mapping_size;

    hugepage_mapping_per_channel.resize(num_host_mem_channels);
    for (uint32_t ch = 0; ch < num_host_mem_channels; ch++) {
        hugepage_mapping_per_channel[ch] = {
            static_cast<uint8_t *>(mapping) + ch * info.channel_size,
            static_cast<size_t>(info.channel_size),
            // On Linux this is the IOVA the device uses to reach the channel; tt-wind's
            // equivalent is the device-PCIe-space (iATU region base) address.
            info.device_io_addr + ch * info.channel_size};
    }

    log_debug(
        LogUMD,
        "Mapped {:#x} bytes of sysmem ({} channel(s) of {:#x} bytes) at NOC address {:#x}.",
        mapping_size,
        num_host_mem_channels,
        info.channel_size,
        info.noc_address);

    return true;
}

bool SiliconSysmemManager::pin_or_map_sysmem_to_device() {
    // Nothing to do: the driver pinned the buffer (it is a boot-time contiguous kernel
    // allocation) and armed the iATU when the device started, and re-arms it after every reset.
    // The device-visible addresses were already recorded in init_sysmem().
    return true;
}

void SiliconSysmemManager::unpin_or_unmap_sysmem() {
    if (iommu_mapping != nullptr) {
        // The driver's UNMAP_BAR path tears down sysmem views too; per-handle cleanup on device
        // close would also catch it, but be tidy for long-lived processes.
        int err = tt_windows_unmap_bar(pci_device_->get_tt_device_handle(), iommu_mapping);
        if (err != 0) {
            log_warning(LogUMD, "Unmapping sysmem failed: {}", strerror(-err));
        }
        iommu_mapping = nullptr;
        iommu_mapping_size = 0;
    }
    hugepage_mapping_per_channel.clear();
}

// PinnedMemory-style buffers (map an arbitrary user allocation so the device can DMA to it) need
// the driver to pin user pages and make them device-reachable (IOMMU or iATU region carve-out).
// tt-wind exposes only the single driver-owned sysmem buffer today, so these stay unsupported.
std::unique_ptr<SysmemBuffer> SiliconSysmemManager::allocate_sysmem_buffer(
    size_t /*sysmem_buffer_size*/, const bool /*map_to_noc*/) {
    UMD_THROW(
        error::RuntimeError,
        "allocate_sysmem_buffer is not yet supported on Windows: the tt-wind driver cannot pin and map arbitrary "
        "user buffers for device access.");
}

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::map_sysmem_buffer(
    void * /*buffer*/, size_t /*sysmem_buffer_size*/, const bool /*map_to_noc*/, DeviceBufferAccess /*device_access*/) {
    UMD_THROW(
        error::RuntimeError,
        "map_sysmem_buffer is not yet supported on Windows: the tt-wind driver cannot pin and map arbitrary "
        "user buffers for device access.");
}

}  // namespace tt::umd
