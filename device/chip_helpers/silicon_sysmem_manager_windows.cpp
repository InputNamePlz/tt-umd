/*
 * SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Windows stub for silicon_sysmem_manager.cpp. The Linux implementation maps hugepages and pins
// IOMMU-backed buffers with mmap/MAP_HUGETLB, which has no Windows implementation yet. Every
// operation throws a "not yet supported on Windows" error.

#include "umd/device/chip_helpers/silicon_sysmem_manager.hpp"

#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {
[[noreturn]] void throw_not_supported() {
    UMD_THROW(error::RuntimeError, "Silicon system memory (sysmem) is not yet supported on Windows.");
}
}  // namespace

SiliconSysmemManager::SiliconSysmemManager(TTDevice *tt_device, uint32_t /*num_host_mem_channels*/) {
    tt_device_ = tt_device;
    throw_not_supported();
}

SiliconSysmemManager::~SiliconSysmemManager() {}

bool SiliconSysmemManager::pin_or_map_sysmem_to_device() { throw_not_supported(); }

void SiliconSysmemManager::unpin_or_unmap_sysmem() { throw_not_supported(); }

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::allocate_sysmem_buffer(
    size_t /*sysmem_buffer_size*/, const bool /*map_to_noc*/) {
    throw_not_supported();
}

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::map_sysmem_buffer(
    void * /*buffer*/, size_t /*sysmem_buffer_size*/, const bool /*map_to_noc*/, DeviceBufferAccess /*device_access*/) {
    throw_not_supported();
}

bool SiliconSysmemManager::init_sysmem(uint32_t /*num_host_mem_channels*/) { throw_not_supported(); }

}  // namespace tt::umd
