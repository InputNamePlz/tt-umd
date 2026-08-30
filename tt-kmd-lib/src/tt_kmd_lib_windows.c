// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Windows placeholder implementation of the tt-kmd-lib public API.
//
// There is no Windows kernel-mode driver yet; until it (tt-wind) lands, every
// entry point fails with -ENOSYS ("function not implemented"). Out-parameters
// are zeroed/nulled before returning so callers never observe garbage.

#include <errno.h>
#include <string.h>

#include "tt-kmd-lib/tt_kmd_lib.h"

int tt_device_open(const char* chardev_path, tt_device_t** out_dev, int extra_flags) {
    (void)chardev_path;
    (void)extra_flags;
    if (out_dev) {
        *out_dev = NULL;
    }
    return -ENOSYS;
}

int tt_device_close(tt_device_t* dev) {
    (void)dev;
    return -ENOSYS;
}

int tt_device_get_attr(tt_device_t* dev, enum tt_device_attr attr, uint64_t* out_value) {
    (void)dev;
    (void)attr;
    if (out_value) {
        *out_value = 0;
    }
    return -ENOSYS;
}

int tt_device_get_attrs(tt_device_t* dev, tt_device_attrs_t* out_attrs) {
    (void)dev;
    if (out_attrs) {
        memset(out_attrs, 0, sizeof(*out_attrs));
    }
    return -ENOSYS;
}

int tt_driver_get_attr(tt_device_t* dev, enum tt_driver_attr attr, uint64_t* out_value) {
    (void)dev;
    (void)attr;
    if (out_value) {
        *out_value = 0;
    }
    return -ENOSYS;
}

int tt_device_query_bar_mappings(tt_device_t* dev, tt_bar_mappings_t* out_mappings) {
    (void)dev;
    if (out_mappings) {
        memset(out_mappings, 0, sizeof(*out_mappings));
    }
    return -ENOSYS;
}

int tt_noc_read32(tt_device_t* dev, uint8_t x, uint8_t y, uint64_t addr, uint32_t* value) {
    (void)dev;
    (void)x;
    (void)y;
    (void)addr;
    if (value) {
        *value = 0;
    }
    return -ENOSYS;
}

int tt_noc_write32(tt_device_t* dev, uint8_t x, uint8_t y, uint64_t addr, uint32_t value) {
    (void)dev;
    (void)x;
    (void)y;
    (void)addr;
    (void)value;
    return -ENOSYS;
}

int tt_noc_read(tt_device_t* dev, uint8_t x, uint8_t y, uint64_t addr, void* dst, size_t len) {
    (void)dev;
    (void)x;
    (void)y;
    (void)addr;
    (void)dst;
    (void)len;
    return -ENOSYS;
}

int tt_noc_write(tt_device_t* dev, uint8_t x, uint8_t y, uint64_t addr, const void* src, size_t len) {
    (void)dev;
    (void)x;
    (void)y;
    (void)addr;
    (void)src;
    (void)len;
    return -ENOSYS;
}

int tt_dma_map(tt_device_t* dev, void* addr, size_t len, int flags, tt_dma_t** out_dma) {
    (void)dev;
    (void)addr;
    (void)len;
    (void)flags;
    if (out_dma) {
        *out_dma = NULL;
    }
    return -ENOSYS;
}

int tt_dma_unmap(tt_device_t* dev, tt_dma_t* dma) {
    (void)dev;
    (void)dma;
    return -ENOSYS;
}

int tt_dma_get_dma_addr(tt_dma_t* dma, uint64_t* out_dma_addr) {
    (void)dma;
    if (out_dma_addr) {
        *out_dma_addr = 0;
    }
    return -ENOSYS;
}

int tt_dma_get_noc_addr(tt_dma_t* dma, uint64_t* out_noc_addr) {
    (void)dma;
    if (out_noc_addr) {
        *out_noc_addr = 0;
    }
    return -ENOSYS;
}

int tt_pin_pages(tt_device_t* dev, void* addr, size_t len, int flags, uint64_t* out_dma_addr, uint64_t* out_noc_addr) {
    (void)dev;
    (void)addr;
    (void)len;
    (void)flags;
    if (out_dma_addr) {
        *out_dma_addr = 0;
    }
    if (out_noc_addr) {
        *out_noc_addr = 0;
    }
    return -ENOSYS;
}

int tt_unpin_pages(tt_device_t* dev, void* addr, size_t len) {
    (void)dev;
    (void)addr;
    (void)len;
    return -ENOSYS;
}

int tt_allocate_dma_buf(
    tt_device_t* dev,
    uint8_t buf_index,
    size_t size,
    int flags,
    void** out_mapping,
    uint64_t* out_dma_addr,
    uint64_t* out_noc_addr) {
    (void)dev;
    (void)buf_index;
    (void)size;
    (void)flags;
    if (out_mapping) {
        *out_mapping = NULL;
    }
    if (out_dma_addr) {
        *out_dma_addr = 0;
    }
    if (out_noc_addr) {
        *out_noc_addr = 0;
    }
    return -ENOSYS;
}

int tt_tlb_alloc(tt_device_t* dev, size_t size, enum tt_tlb_cache_mode cache, tt_tlb_t** out_tlb) {
    (void)dev;
    (void)size;
    (void)cache;
    if (out_tlb) {
        *out_tlb = NULL;
    }
    return -ENOSYS;
}

int tt_tlb_free(tt_device_t* dev, tt_tlb_t* tlb) {
    (void)dev;
    (void)tlb;
    return -ENOSYS;
}

int tt_tlb_get_mmio(tt_tlb_t* tlb, void** out_mmio) {
    (void)tlb;
    if (out_mmio) {
        *out_mmio = NULL;
    }
    return -ENOSYS;
}

int tt_tlb_get_id(tt_tlb_t* tlb, uint32_t* out_id) {
    (void)tlb;
    if (out_id) {
        *out_id = 0;
    }
    return -ENOSYS;
}

int tt_tlb_map(tt_device_t* dev, tt_tlb_t* tlb, tt_noc_addr_config_t* config) {
    (void)dev;
    (void)tlb;
    (void)config;
    return -ENOSYS;
}

int tt_tlb_map_unicast(tt_device_t* dev, tt_tlb_t* tlb, uint8_t x, uint8_t y, uint64_t addr) {
    (void)dev;
    (void)tlb;
    (void)x;
    (void)y;
    (void)addr;
    return -ENOSYS;
}

int tt_tlb_export_dmabuf(tt_device_t* dev, tt_tlb_t* tlb, uint64_t offset, uint64_t size, int* out_fd) {
    (void)dev;
    (void)tlb;
    (void)offset;
    (void)size;
    if (out_fd) {
        *out_fd = -1;
    }
    return -ENOSYS;
}

int tt_device_set_power_state(tt_device_t* dev, uint16_t power_flags) {
    (void)dev;
    (void)power_flags;
    return -ENOSYS;
}

int tt_device_reset(tt_device_t* dev, uint32_t reset_flags) {
    (void)dev;
    (void)reset_flags;
    return -ENOSYS;
}

int tt_lock_acquire(tt_device_t* dev, uint8_t index, int* out_acquired) {
    (void)dev;
    (void)index;
    if (out_acquired) {
        *out_acquired = 0;
    }
    return -ENOSYS;
}

int tt_lock_release(tt_device_t* dev, uint8_t index, int* out_was_held) {
    (void)dev;
    (void)index;
    if (out_was_held) {
        *out_was_held = 0;
    }
    return -ENOSYS;
}

int tt_lock_test(tt_device_t* dev, uint8_t index, uint32_t* out_state) {
    (void)dev;
    (void)index;
    if (out_state) {
        *out_state = 0;
    }
    return -ENOSYS;
}
