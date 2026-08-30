// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef TT_KMD_LIB_WINDOWS_H_
#define TT_KMD_LIB_WINDOWS_H_

// Windows-only extensions to the tt_kmd_lib.h API, implemented by the tt-wind
// backend (tt_kmd_lib_windows.c). On Linux, BAR regions are mapped with mmap()
// on the device file descriptor using the offsets from
// tt_device_query_bar_mappings(); Windows has no such mechanism, so mapping and
// unmapping go through these entry points instead. All functions follow the
// same 0 / -errno convention as tt_kmd_lib.h.

#include <stdint.h>

#include "tt-kmd-lib/tt_kmd_lib.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Number of Tenstorrent devices currently present.
 *
 * Counts present device interfaces of the tt-wind driver. Ordinals in
 * [0, count) can be passed to tt_device_open() as the chardev path (either
 * bare, "0", or Linux-style, "/dev/tenstorrent/0"); ordinal order is stable
 * because the interface path list is sorted.
 *
 * @return Nonnegative device count on success, negative error code on failure.
 */
int tt_windows_device_count(void);

/**
 * @brief Map a slice of a PCI BAR into the calling process.
 *
 * The driver caps a single mapping at 8 MiB; larger regions must be mapped in
 * chunks. Offset and length must be page aligned.
 *
 * @param dev Device handle
 * @param bar_index PCI BAR number (0, 2 or 4; 64-bit BARs use their low index)
 * @param cache TT_MMIO_CACHE_MODE_UC for registers, .._WC for bulk data
 * @param offset Byte offset into the BAR; page aligned
 * @param length Bytes to map; page aligned, nonzero, at most 8 MiB
 * @param out_va On success, base of the new mapping
 * @return 0 on success, negative error code on failure
 */
int tt_windows_map_bar(
    tt_device_t* dev,
    uint32_t bar_index,
    enum tt_tlb_cache_mode cache,
    uint64_t offset,
    uint64_t length,
    void** out_va);

/**
 * @brief Unmap a mapping created by tt_windows_map_bar().
 *
 * @param dev Device handle the mapping was created on
 * @param va Exact base address returned by tt_windows_map_bar()
 * @return 0 on success, negative error code on failure
 */
int tt_windows_unmap_bar(tt_device_t* dev, void* va);

/**
 * @brief Description of the driver-owned host system memory (sysmem) buffer.
 *
 * tt-wind allocates one physically contiguous, cached host buffer at device
 * start and exposes it to the chip through outbound iATU region 0 of the PCIe
 * controller: NOC requests addressed to [noc_address, noc_address + total_size)
 * on the PCIe tile land in the buffer.
 */
typedef struct tt_sysmem_info {
    uint64_t total_size;     /**< Bytes of sysmem; 0 = sysmem unavailable (all other fields 0). */
    uint64_t noc_address;    /**< NOC address of byte 0 as seen from any tile (Blackhole: 4 << 58). */
    uint64_t device_io_addr; /**< Device-PCIe-space (iATU region base) address of byte 0. */
    uint64_t channel_size;   /**< Bytes per channel; channel_count * channel_size == total_size. */
    uint32_t channel_count;  /**< Number of channels the driver carved the buffer into. */
    uint32_t max_map_bytes;  /**< Largest length a single tt_windows_map_sysmem() accepts. */
    uint32_t pcie_tile_x;    /**< NOC0 x-coordinate of the active PCIe tile (y is 0). */
} tt_sysmem_info_t;

/**
 * @brief Query the driver-owned sysmem buffer.
 *
 * Succeeds with out_info->total_size == 0 when sysmem is unavailable (the
 * boot-time contiguous allocation or the iATU arming failed, or a reset is in
 * flight); callers must treat that as "no sysmem", not as an error.
 *
 * @param dev Device handle
 * @param out_info Filled on success; zeroed on failure
 * @return 0 on success, negative error code on failure
 */
int tt_windows_query_sysmem(tt_device_t* dev, tt_sysmem_info_t* out_info);

/**
 * @brief Map a slice of the sysmem buffer into the calling process.
 *
 * The mapping is a single contiguous, cached, read/write view (the buffer is
 * cached host RAM; PCIe DMA is cache-coherent on x64). Offset and length must
 * be page aligned, length nonzero and at most tt_sysmem_info_t::max_map_bytes,
 * and the range in bounds. Unmap with tt_windows_unmap_bar() (the driver's
 * UNMAP_BAR path handles sysmem views too); mappings still live when the
 * device handle closes are torn down automatically.
 *
 * @param dev Device handle
 * @param offset Byte offset into the sysmem buffer; page aligned
 * @param length Bytes to map; page aligned, nonzero
 * @param out_va On success, base of the new mapping
 * @return 0 on success, negative error code on failure
 */
int tt_windows_map_sysmem(tt_device_t* dev, uint64_t offset, uint64_t length, void** out_va);

/**
 * @brief Configure a TLB window by its driver-assigned id.
 *
 * Driver-mediated equivalent of tt_tlb_map() for callers that track windows by
 * id (see tt_tlb_get_id()) rather than by handle. The window must have been
 * allocated on this same device handle.
 *
 * @param dev Device handle
 * @param tlb_id Window id from tt_tlb_get_id()
 * @param config NOC address configuration; config->addr must be aligned to the
 *               window size
 * @return 0 on success, negative error code on failure
 */
int tt_windows_configure_tlb(tt_device_t* dev, uint32_t tlb_id, const tt_noc_addr_config_t* config);

#ifdef __cplusplus
}
#endif

#endif  // TT_KMD_LIB_WINDOWS_H_
