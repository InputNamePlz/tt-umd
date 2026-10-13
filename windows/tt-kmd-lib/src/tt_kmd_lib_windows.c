// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Windows implementation of the tt-kmd-lib public API, backed by the tt-wind
// kernel-mode driver.
//
// Devices are enumerated through the tt-wind device interface class
// (GUID_DEVINTERFACE_TTWIND) via the Configuration Manager API and opened with
// CreateFile. The interface path list is sorted so that an ordinal index is
// stable across enumerations; tt_device_open() accepts either a full interface
// path ("\\?\...") or a Linux-style path whose last component is the ordinal
// ("/dev/tenstorrent/0", or just "0").
//
// Device reset is wired to IOCTL_TTWIND_RESET_DEVICE, and the 64 per-device
// resource locks are implemented with named kernel mutexes (see the tt_lock_*
// section). Functionality the driver does not implement yet (DMA-buffer
// allocation, page pinning, dma-buf export, power state, driver version query)
// keeps returning -ENOSYS. Out-parameters are zeroed/nulled on failure so
// callers never observe garbage.

#include <windows.h>
#include <winioctl.h>
#include <cfgmgr32.h>
#include <initguid.h> /* instantiate GUID_DEVINTERFACE_TTWIND in this TU */

#include <errno.h>
#include <stdint.h>
#include <stdio.h> /* swprintf */
#include <stdlib.h>
#include <string.h>

#include "ttwind_ioctl.h"

#include "tt-kmd-lib/pci_ids.h"
#include "tt-kmd-lib/tt_kmd_lib.h"
#include "tt-kmd-lib/tt_kmd_lib_windows.h"

#pragma comment(lib, "cfgmgr32.lib")

#define MIN(a, b) ((a) < (b) ? (a) : (b))

struct tt_device_t {
    HANDLE handle;

    /* Resource locks (tt_lock_*): lazily created named mutexes plus this
     * handle's hold count per slot and the slot's index (biased by one, so
     * zero means unset) in the process-global lock registry; see the
     * tt_lock_* section. */
    HANDLE lock_handles[TT_RESOURCE_LOCK_COUNT];
    unsigned lock_held_count[TT_RESOURCE_LOCK_COUNT];
    unsigned lock_registry_slot_plus1[TT_RESOURCE_LOCK_COUNT];
};

struct tt_tlb_t {
    uint32_t id;
    size_t size;
    void* mmio;
};

/* --- in-process lock registry ------------------------------------------- */

/* Process-global registry of the named lock mutexes this process currently
 * holds. Win32 mutexes are recursive per owning thread: once any handle in
 * this process holds a slot's mutex, a second zero-timeout wait issued on the
 * owning thread succeeds recursively instead of failing. The driver's locks
 * are not recursive -- an acquire fails while the lock is held, even when the
 * asking handle is the holder -- so in-process arbitration happens here, and
 * only cross-process contention is left to the kernel mutex (which also
 * provides crash recovery via WAIT_ABANDONED). See the tt_lock_* section. */
#define LOCK_NAME_CHARS 128

typedef struct lock_registry_entry {
    wchar_t name[LOCK_NAME_CHARS];
    int held; /* some handle in this process holds the named mutex */
} lock_registry_entry;

static INIT_ONCE g_lock_registry_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION g_lock_registry_cs;
static lock_registry_entry* g_lock_registry = NULL;
static size_t g_lock_registry_count = 0;
static size_t g_lock_registry_cap = 0;

static BOOL CALLBACK lock_registry_init_once(PINIT_ONCE once, PVOID param, PVOID* context) {
    (void)once;
    (void)param;
    (void)context;
    InitializeCriticalSection(&g_lock_registry_cs);
    return TRUE;
}

static void lock_registry_enter(void) {
    InitOnceExecuteOnce(&g_lock_registry_once, lock_registry_init_once, NULL, NULL);
    EnterCriticalSection(&g_lock_registry_cs);
}

static void lock_registry_leave(void) { LeaveCriticalSection(&g_lock_registry_cs); }

/* Caller must hold the registry critical section. Returns the entry index for
 * name, adding the entry if it is new, or -1 on allocation failure. */
static int lock_registry_find_or_add(const wchar_t* name) {
    for (size_t i = 0; i < g_lock_registry_count; i++) {
        if (wcscmp(g_lock_registry[i].name, name) == 0) {
            return (int)i;
        }
    }
    if (g_lock_registry_count == g_lock_registry_cap) {
        size_t new_cap = g_lock_registry_cap ? g_lock_registry_cap * 2 : 16;
        lock_registry_entry* grown = (lock_registry_entry*)realloc(g_lock_registry, new_cap * sizeof(*grown));
        if (grown == NULL) {
            return -1;
        }
        g_lock_registry = grown;
        g_lock_registry_cap = new_cap;
    }
    lock_registry_entry* entry = &g_lock_registry[g_lock_registry_count];
    wcscpy_s(entry->name, LOCK_NAME_CHARS, name);
    entry->held = 0;
    return (int)(g_lock_registry_count++);
}

/* --- error translation ------------------------------------------------- */

/* Translate a Win32 error code (GetLastError) into a negative errno value,
 * matching the 0/-errno convention of the tt_kmd_lib.h API. */
static int win_err_to_errno(DWORD err) {
    switch (err) {
        case ERROR_SUCCESS:
            return 0;
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_NO_MORE_ITEMS:
        case ERROR_DEVICE_NOT_CONNECTED:
        case ERROR_NOT_FOUND:
            return -ENODEV;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
            return -EACCES;
        case ERROR_INVALID_PARAMETER:
        case ERROR_INVALID_ADDRESS:
        case ERROR_BAD_LENGTH:
            return -EINVAL;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
        case ERROR_NO_SYSTEM_RESOURCES:
            return -ENOMEM;
        case ERROR_INVALID_FUNCTION:
        case ERROR_NOT_SUPPORTED:
            return -ENOSYS;
        case ERROR_BUSY:
        case ERROR_BAD_COMMAND: /* STATUS_INVALID_DEVICE_STATE */
            return -EBUSY;
        case ERROR_INVALID_HANDLE:
            return -EBADF;
        case ERROR_INSUFFICIENT_BUFFER:
            return -ERANGE;
        default:
            return -EIO;
    }
}

static int last_err(void) { return win_err_to_errno(GetLastError()); }

/* Wrapper around DeviceIoControl returning 0 / -errno. */
static int ttwind_ioctl(HANDLE h, DWORD code, const void* in, DWORD in_len, void* out, DWORD out_len) {
    DWORD returned = 0;
    if (!DeviceIoControl(h, code, (LPVOID)in, in_len, out, out_len, &returned, NULL)) {
        return last_err();
    }
    if (out != NULL && returned < out_len) {
        return -EIO; /* short reply: driver/library mismatch */
    }
    return 0;
}

/* --- device interface enumeration -------------------------------------- */

/* Return the REG_MULTI_SZ interface path list for present tt-wind devices, or
 * NULL with *out_err set to a negative errno. Caller frees. */
static wchar_t* get_interface_list(int* out_err) {
    wchar_t* list = NULL;
    ULONG chars = 0;
    CONFIGRET cr;

    /* The required size can change between the two calls if devices arrive or
     * leave; retry until they agree. */
    for (;;) {
        cr = CM_Get_Device_Interface_List_SizeW(
            &chars, (LPGUID)&GUID_DEVINTERFACE_TTWIND, NULL, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
        if (cr != CR_SUCCESS) {
            *out_err = -EIO;
            return NULL;
        }

        free(list);
        list = (wchar_t*)calloc(chars ? chars : 1, sizeof(wchar_t));
        if (list == NULL) {
            *out_err = -ENOMEM;
            return NULL;
        }

        cr = CM_Get_Device_Interface_ListW(
            (LPGUID)&GUID_DEVINTERFACE_TTWIND, NULL, list, chars, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
        if (cr == CR_SUCCESS) {
            *out_err = 0;
            return list;
        }
        if (cr != CR_BUFFER_SMALL) {
            free(list);
            *out_err = -EIO;
            return NULL;
        }
    }
}

static int compare_paths(const void* a, const void* b) {
    return _wcsicmp(*(const wchar_t* const*)a, *(const wchar_t* const*)b);
}

/* Split the multi-sz list into a sorted array of pointers (into the list).
 * Sorting gives a stable ordinal ordering across enumerations. */
static int sorted_interface_paths(wchar_t* list, const wchar_t*** out_paths, unsigned* out_count) {
    unsigned count = 0;
    const wchar_t* p;
    const wchar_t** paths;

    for (p = list; *p != L'\0'; p += wcslen(p) + 1) {
        count++;
    }

    paths = (const wchar_t**)calloc(count ? count : 1, sizeof(*paths));
    if (paths == NULL) {
        return -ENOMEM;
    }

    count = 0;
    for (p = list; *p != L'\0'; p += wcslen(p) + 1) {
        paths[count++] = p;
    }
    qsort((void*)paths, count, sizeof(*paths), compare_paths);

    *out_paths = paths;
    *out_count = count;
    return 0;
}

int tt_windows_device_count(void) {
    int err = 0;
    wchar_t* list = get_interface_list(&err);
    unsigned count = 0;
    const wchar_t* p;

    if (list == NULL) {
        return err;
    }
    for (p = list; *p != L'\0'; p += wcslen(p) + 1) {
        count++;
    }
    free(list);
    return (int)count;
}

/* Parse the last path component of a Linux-style chardev path ("0",
 * "/dev/tenstorrent/0") as a nonnegative ordinal. Returns 0 / -EINVAL. */
static int parse_ordinal(const char* path, unsigned* out_index) {
    const char* last = path;
    const char* p;
    char* end = NULL;
    unsigned long v;

    for (p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            last = p + 1;
        }
    }
    if (*last == '\0') {
        return -EINVAL;
    }

    v = strtoul(last, &end, 10);
    if (end == last || *end != '\0' || v > 0xFFFF) {
        return -EINVAL;
    }
    *out_index = (unsigned)v;
    return 0;
}

static HANDLE open_interface_path_w(const wchar_t* path) {
    return CreateFileW(
        path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
}

int tt_device_open(const char* chardev_path, tt_device_t** out_dev, int extra_flags) {
    /* extra_flags carries Linux open(2) flags (e.g. O_APPEND as a power hint);
     * tt-wind has no equivalent, so they are ignored. */
    (void)extra_flags;

    if (out_dev != NULL) {
        *out_dev = NULL;
    }
    if (chardev_path == NULL || out_dev == NULL) {
        return -EINVAL;
    }

    HANDLE h = INVALID_HANDLE_VALUE;

    if (chardev_path[0] == '\\') {
        /* Full interface path, e.g. "\\?\pci#ven_1e52...#{...}". */
        wchar_t wpath[1024];
        if (MultiByteToWideChar(CP_UTF8, 0, chardev_path, -1, wpath, ARRAYSIZE(wpath)) == 0) {
            return -EINVAL;
        }
        h = open_interface_path_w(wpath);
        if (h == INVALID_HANDLE_VALUE) {
            return last_err();
        }
    } else {
        unsigned index = 0;
        int err = parse_ordinal(chardev_path, &index);
        if (err != 0) {
            return err;
        }

        wchar_t* list = get_interface_list(&err);
        if (list == NULL) {
            return err;
        }

        const wchar_t** paths = NULL;
        unsigned count = 0;
        err = sorted_interface_paths(list, &paths, &count);
        if (err != 0) {
            free(list);
            return err;
        }

        if (index >= count) {
            free((void*)paths);
            free(list);
            return -ENODEV;
        }

        h = open_interface_path_w(paths[index]);
        err = (h == INVALID_HANDLE_VALUE) ? last_err() : 0;
        free((void*)paths);
        free(list);
        if (err != 0) {
            return err;
        }
    }

    struct tt_device_t* dev = (struct tt_device_t*)calloc(1, sizeof(*dev));
    if (dev == NULL) {
        CloseHandle(h);
        return -ENOMEM;
    }

    dev->handle = h;
    *out_dev = dev;
    return 0;
}

int tt_device_close(tt_device_t* dev) {
    if (dev == NULL) {
        return -EINVAL;
    }

    /* Mirror the Linux driver's close semantics: every resource lock this
     * handle still holds is released. ReleaseMutex only succeeds on the
     * owning thread; if close runs on another thread the release fails and
     * the mutex is recovered as abandoned when the owning thread exits. */
    for (unsigned i = 0; i < TT_RESOURCE_LOCK_COUNT; i++) {
        if (dev->lock_handles[i] != NULL) {
            if (dev->lock_held_count[i] > 0) {
                lock_registry_enter();
                while (dev->lock_held_count[i] > 0 && ReleaseMutex(dev->lock_handles[i])) {
                    dev->lock_held_count[i]--;
                }
                if (dev->lock_held_count[i] == 0) {
                    g_lock_registry[dev->lock_registry_slot_plus1[i] - 1].held = 0;
                }
                lock_registry_leave();
            }
            CloseHandle(dev->lock_handles[i]);
        }
    }

    if (!CloseHandle(dev->handle)) {
        int err = last_err();
        free(dev);
        return err;
    }
    free(dev);
    return 0;
}

/* --- device attributes -------------------------------------------------- */

static int get_device_info(tt_device_t* dev, TTWIND_DEVICE_INFO_OUT* info) {
    memset(info, 0, sizeof(*info));
    return ttwind_ioctl(dev->handle, IOCTL_TTWIND_GET_DEVICE_INFO, NULL, 0, info, sizeof(*info));
}

int tt_device_get_attrs(tt_device_t* dev, tt_device_attrs_t* out_attrs) {
    if (dev == NULL || out_attrs == NULL) {
        return -EINVAL;
    }
    memset(out_attrs, 0, sizeof(*out_attrs));

    TTWIND_DEVICE_INFO_OUT info;
    int err = get_device_info(dev, &info);
    if (err != 0) {
        return err;
    }

    uint64_t arch = TT_DEVICE_ARCH_UNKNOWN;
    if (info.DeviceId == TT_BLACKHOLE_PCI_DEVICE_ID) {
        arch = TT_DEVICE_ARCH_BLACKHOLE;
    } else if (info.DeviceId == TT_WORMHOLE_PCI_DEVICE_ID) {
        arch = TT_DEVICE_ARCH_WORMHOLE;
    }

    out_attrs->pci_domain = info.PciDomain;
    out_attrs->pci_bus = info.Bus;
    out_attrs->pci_device = info.Device;
    out_attrs->pci_function = info.Function;
    out_attrs->pci_vendor_id = info.VendorId;
    out_attrs->pci_device_id = info.DeviceId;
    out_attrs->pci_subsystem_vendor_id = info.SubsystemVendorId;
    out_attrs->pci_subsystem_id = info.SubsystemId;
    out_attrs->chip_arch = arch;

    /* Same per-architecture window counts as the Linux backend. The driver
     * only implements the Blackhole 2 MiB windows today. */
    switch (arch) {
        case TT_DEVICE_ARCH_WORMHOLE:
            out_attrs->num_1m_tlbs = 156;
            out_attrs->num_2m_tlbs = 10;
            out_attrs->num_16m_tlbs = 20;
            break;
        case TT_DEVICE_ARCH_BLACKHOLE:
            out_attrs->num_2m_tlbs = TTWIND_TLB_2M_WINDOW_COUNT;
            out_attrs->num_4g_tlbs = 8;
            break;
        default:
            break;
    }

    return 0;
}

int tt_device_get_attr(tt_device_t* dev, enum tt_device_attr attr, uint64_t* out_value) {
    if (out_value == NULL) {
        return -EINVAL;
    }
    *out_value = 0;

    tt_device_attrs_t attrs;
    int err = tt_device_get_attrs(dev, &attrs);
    if (err != 0) {
        return err;
    }

    switch (attr) {
        case TT_DEVICE_ATTR_PCI_DOMAIN:
            *out_value = attrs.pci_domain;
            break;
        case TT_DEVICE_ATTR_PCI_BUS:
            *out_value = attrs.pci_bus;
            break;
        case TT_DEVICE_ATTR_PCI_DEVICE:
            *out_value = attrs.pci_device;
            break;
        case TT_DEVICE_ATTR_PCI_FUNCTION:
            *out_value = attrs.pci_function;
            break;
        case TT_DEVICE_ATTR_PCI_VENDOR_ID:
            *out_value = attrs.pci_vendor_id;
            break;
        case TT_DEVICE_ATTR_PCI_DEVICE_ID:
            *out_value = attrs.pci_device_id;
            break;
        case TT_DEVICE_ATTR_PCI_SUBSYSTEM_VENDOR_ID:
            *out_value = attrs.pci_subsystem_vendor_id;
            break;
        case TT_DEVICE_ATTR_PCI_SUBSYSTEM_ID:
            *out_value = attrs.pci_subsystem_id;
            break;
        case TT_DEVICE_ATTR_CHIP_ARCH:
            *out_value = attrs.chip_arch;
            break;
        case TT_DEVICE_ATTR_NUM_1M_TLBS:
            *out_value = attrs.num_1m_tlbs;
            break;
        case TT_DEVICE_ATTR_NUM_2M_TLBS:
            *out_value = attrs.num_2m_tlbs;
            break;
        case TT_DEVICE_ATTR_NUM_16M_TLBS:
            *out_value = attrs.num_16m_tlbs;
            break;
        case TT_DEVICE_ATTR_NUM_4G_TLBS:
            *out_value = attrs.num_4g_tlbs;
            break;
        default:
            return -EINVAL;
    }

    return 0;
}

int tt_driver_get_attr(tt_device_t* dev, enum tt_driver_attr attr, uint64_t* out_value) {
    /* tt-wind has no driver-version query yet. */
    (void)dev;
    (void)attr;
    if (out_value) {
        *out_value = 0;
    }
    return -ENOSYS;
}

/* --- BAR mappings ------------------------------------------------------- */

/* Resource numbering follows the Linux backend: resource0 = BAR0,
 * resource1 = BAR2, resource2 = BAR4 (64-bit BARs occupy the even indices).
 *
 * On Linux `base` is an mmap() offset token; there is no mmap on Windows, so
 * base is 0 here and mappings are created with tt_windows_map_bar(), which
 * takes the BAR index and a byte offset directly. `size` is the BAR size. */
int tt_device_query_bar_mappings(tt_device_t* dev, tt_bar_mappings_t* out_mappings) {
    if (dev == NULL || out_mappings == NULL) {
        return -EINVAL;
    }
    memset(out_mappings, 0, sizeof(*out_mappings));

    TTWIND_DEVICE_INFO_OUT info;
    int err = get_device_info(dev, &info);
    if (err != 0) {
        return err;
    }

    struct {
        unsigned bar;
        tt_bar_mapping_t* uc;
        tt_bar_mapping_t* wc;
        uint32_t uc_id;
        uint32_t wc_id;
    } const table[3] = {
        {0u, &out_mappings->resource0_uc, &out_mappings->resource0_wc, TT_BAR_MAPPING_RESOURCE0_UC,
         TT_BAR_MAPPING_RESOURCE0_WC},
        {2u, &out_mappings->resource1_uc, &out_mappings->resource1_wc, TT_BAR_MAPPING_RESOURCE1_UC,
         TT_BAR_MAPPING_RESOURCE1_WC},
        {4u, &out_mappings->resource2_uc, &out_mappings->resource2_wc, TT_BAR_MAPPING_RESOURCE2_UC,
         TT_BAR_MAPPING_RESOURCE2_WC},
    };

    for (unsigned i = 0; i < 3; i++) {
        const TTWIND_BAR_DESC* bar = &info.Bars[table[i].bar];
        if (bar->Size == 0) {
            continue;
        }
        table[i].uc->id = table[i].uc_id;
        table[i].uc->base = 0;
        table[i].uc->size = bar->Size;
        table[i].wc->id = table[i].wc_id;
        table[i].wc->base = 0;
        table[i].wc->size = bar->Size;
    }

    return 0;
}

int tt_windows_map_bar(
    tt_device_t* dev, uint32_t bar_index, enum tt_tlb_cache_mode cache, uint64_t offset, uint64_t length,
    void** out_va) {
    if (out_va != NULL) {
        *out_va = NULL;
    }
    if (dev == NULL || out_va == NULL || bar_index >= TTWIND_MAX_BARS) {
        return -EINVAL;
    }
    if (length == 0 || length > TTWIND_MAX_MAP_BYTES || (offset & 0xFFFu) != 0 || (length & 0xFFFu) != 0) {
        return -EINVAL;
    }

    TTWIND_MAP_BAR_IN in;
    TTWIND_MAP_BAR_OUT out;
    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.BarIndex = bar_index;
    in.CacheMode = (cache == TT_MMIO_CACHE_MODE_WC) ? TTWIND_CACHE_WC : TTWIND_CACHE_UC;
    in.Offset = offset;
    in.Length = length;

    int err = ttwind_ioctl(dev->handle, IOCTL_TTWIND_MAP_BAR, &in, sizeof(in), &out, sizeof(out));
    if (err != 0) {
        return err;
    }

    *out_va = (void*)(uintptr_t)out.UserVa;
    return 0;
}

int tt_windows_unmap_bar(tt_device_t* dev, void* va) {
    if (dev == NULL || va == NULL) {
        return -EINVAL;
    }

    TTWIND_UNMAP_BAR_IN in;
    memset(&in, 0, sizeof(in));
    in.UserVa = (uint64_t)(uintptr_t)va;
    return ttwind_ioctl(dev->handle, IOCTL_TTWIND_UNMAP_BAR, &in, sizeof(in), NULL, 0);
}

/* --- host system memory (sysmem) ---------------------------------------- */

int tt_windows_query_sysmem(tt_device_t* dev, tt_sysmem_info_t* out_info) {
    if (out_info != NULL) {
        memset(out_info, 0, sizeof(*out_info));
    }
    if (dev == NULL || out_info == NULL) {
        return -EINVAL;
    }

    TTWIND_QUERY_SYSMEM_OUT out;
    memset(&out, 0, sizeof(out));
    int err = ttwind_ioctl(dev->handle, IOCTL_TTWIND_QUERY_SYSMEM, NULL, 0, &out, sizeof(out));
    if (err != 0) {
        return err;
    }

    out_info->total_size = out.TotalSize;
    out_info->noc_address = out.NocAddress;
    out_info->device_io_addr = out.DeviceIoAddr;
    out_info->channel_size = out.ChannelSize;
    out_info->channel_count = out.ChannelCount;
    out_info->max_map_bytes = out.MaxMapBytes;
    out_info->pcie_tile_x = out.PcieTileX;
    return 0;
}

int tt_windows_map_sysmem(tt_device_t* dev, uint64_t offset, uint64_t length, void** out_va) {
    if (out_va != NULL) {
        *out_va = NULL;
    }
    if (dev == NULL || out_va == NULL) {
        return -EINVAL;
    }
    if (length == 0 || (offset & 0xFFFu) != 0 || (length & 0xFFFu) != 0) {
        return -EINVAL;
    }

    TTWIND_MAP_SYSMEM_IN in;
    TTWIND_MAP_SYSMEM_OUT out;
    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.Offset = offset;
    in.Length = length;

    int err = ttwind_ioctl(dev->handle, IOCTL_TTWIND_MAP_SYSMEM, &in, sizeof(in), &out, sizeof(out));
    if (err != 0) {
        return err;
    }

    *out_va = (void*)(uintptr_t)out.UserVa;
    return 0;
}

/* --- TLB windows -------------------------------------------------------- */

static void fill_tlb_config(TTWIND_NOC_TLB_CONFIG* out, const tt_noc_addr_config_t* config) {
    memset(out, 0, sizeof(*out));
    out->Addr = config->addr;
    out->XEnd = config->x_end;
    out->YEnd = config->y_end;
    out->XStart = config->x_start;
    out->YStart = config->y_start;
    out->Noc = config->noc;
    out->Mcast = config->mcast;
    out->Ordering = config->ordering;
    out->Linked = 0; /* not exposed by tt_noc_addr_config_t */
    out->StaticVc = config->static_vc;
}

int tt_windows_configure_tlb(tt_device_t* dev, uint32_t tlb_id, const tt_noc_addr_config_t* config) {
    if (dev == NULL || config == NULL) {
        return -EINVAL;
    }

    TTWIND_CONFIGURE_TLB_IN in;
    memset(&in, 0, sizeof(in));
    in.TlbId = tlb_id;
    fill_tlb_config(&in.Config, config);
    return ttwind_ioctl(dev->handle, IOCTL_TTWIND_CONFIGURE_TLB, &in, sizeof(in), NULL, 0);
}

int tt_tlb_alloc(tt_device_t* dev, size_t size, enum tt_tlb_cache_mode cache, tt_tlb_t** out_tlb) {
    if (out_tlb != NULL) {
        *out_tlb = NULL;
    }
    if (dev == NULL || out_tlb == NULL) {
        return -EINVAL;
    }
    /* The driver only implements Blackhole's 2 MiB windows for now. */
    if (size != TT_TLB_SIZE_2M) {
        return -EINVAL;
    }

    struct tt_tlb_t* tlb = (struct tt_tlb_t*)calloc(1, sizeof(*tlb));
    if (tlb == NULL) {
        return -ENOMEM;
    }

    TTWIND_ALLOCATE_TLB_IN alloc_in;
    TTWIND_ALLOCATE_TLB_OUT alloc_out;
    memset(&alloc_in, 0, sizeof(alloc_in));
    memset(&alloc_out, 0, sizeof(alloc_out));
    alloc_in.Size = size;

    int err = ttwind_ioctl(dev->handle, IOCTL_TTWIND_ALLOCATE_TLB, &alloc_in, sizeof(alloc_in), &alloc_out,
                           sizeof(alloc_out));
    if (err != 0) {
        free(tlb);
        return err;
    }

    TTWIND_MAP_TLB_IN map_in;
    TTWIND_MAP_TLB_OUT map_out;
    memset(&map_in, 0, sizeof(map_in));
    memset(&map_out, 0, sizeof(map_out));
    map_in.TlbId = alloc_out.TlbId;
    map_in.CacheMode = (cache == TT_MMIO_CACHE_MODE_WC) ? TTWIND_CACHE_WC : TTWIND_CACHE_UC;

    err = ttwind_ioctl(dev->handle, IOCTL_TTWIND_MAP_TLB, &map_in, sizeof(map_in), &map_out, sizeof(map_out));
    if (err != 0) {
        TTWIND_FREE_TLB_IN free_in;
        memset(&free_in, 0, sizeof(free_in));
        free_in.TlbId = alloc_out.TlbId;
        (void)ttwind_ioctl(dev->handle, IOCTL_TTWIND_FREE_TLB, &free_in, sizeof(free_in), NULL, 0);
        free(tlb);
        return err;
    }

    tlb->id = alloc_out.TlbId;
    tlb->size = size;
    tlb->mmio = (void*)(uintptr_t)map_out.UserVa;
    *out_tlb = tlb;
    return 0;
}

int tt_tlb_free(tt_device_t* dev, tt_tlb_t* tlb) {
    if (dev == NULL || tlb == NULL) {
        return -EINVAL;
    }

    int ret = 0;

    /* The driver refuses FREE_TLB while a user mapping is live; unmap first. */
    TTWIND_UNMAP_BAR_IN unmap_in;
    memset(&unmap_in, 0, sizeof(unmap_in));
    unmap_in.UserVa = (uint64_t)(uintptr_t)tlb->mmio;
    ret = ttwind_ioctl(dev->handle, IOCTL_TTWIND_UNMAP_BAR, &unmap_in, sizeof(unmap_in), NULL, 0);

    TTWIND_FREE_TLB_IN free_in;
    memset(&free_in, 0, sizeof(free_in));
    free_in.TlbId = tlb->id;
    int free_ret = ttwind_ioctl(dev->handle, IOCTL_TTWIND_FREE_TLB, &free_in, sizeof(free_in), NULL, 0);
    if (ret == 0) {
        ret = free_ret;
    }

    free(tlb);
    return ret;
}

int tt_tlb_get_mmio(tt_tlb_t* tlb, void** out_mmio) {
    if (tlb == NULL || out_mmio == NULL) {
        return -EINVAL;
    }
    *out_mmio = tlb->mmio;
    return 0;
}

int tt_tlb_get_id(tt_tlb_t* tlb, uint32_t* out_id) {
    if (tlb == NULL || out_id == NULL) {
        return -EINVAL;
    }
    *out_id = tlb->id;
    return 0;
}

int tt_tlb_map(tt_device_t* dev, tt_tlb_t* tlb, tt_noc_addr_config_t* config) {
    if (dev == NULL || tlb == NULL || config == NULL) {
        return -EINVAL;
    }
    if (config->addr & (tlb->size - 1)) {
        return -EINVAL;
    }
    return tt_windows_configure_tlb(dev, tlb->id, config);
}

int tt_tlb_map_unicast(tt_device_t* dev, tt_tlb_t* tlb, uint8_t x, uint8_t y, uint64_t addr) {
    tt_noc_addr_config_t config;
    memset(&config, 0, sizeof(config));
    config.addr = addr;
    config.x_end = x;
    config.y_end = y;
    return tt_tlb_map(dev, tlb, &config);
}

/* --- NOC convenience access (built on TLB windows, like the Linux backend) */

int tt_noc_read32(tt_device_t* dev, uint8_t x, uint8_t y, uint64_t addr, uint32_t* value) {
    if (value == NULL) {
        return -EINVAL;
    }
    *value = 0;
    if (addr % 4 != 0) {
        return -EINVAL;
    }

    tt_tlb_t* tlb = NULL;
    int ret = tt_tlb_alloc(dev, TT_TLB_SIZE_2M, TT_MMIO_CACHE_MODE_UC, &tlb);
    if (ret != 0) {
        return ret;
    }

    uint64_t aligned_addr = addr & ~((uint64_t)tlb->size - 1);
    ret = tt_tlb_map_unicast(dev, tlb, x, y, aligned_addr);
    if (ret != 0) {
        tt_tlb_free(dev, tlb);
        return ret;
    }

    uint64_t offset = addr & (tlb->size - 1);
    *value = *(volatile uint32_t*)((uint8_t*)tlb->mmio + offset);

    tt_tlb_free(dev, tlb);
    return 0;
}

int tt_noc_write32(tt_device_t* dev, uint8_t x, uint8_t y, uint64_t addr, uint32_t value) {
    if (addr % 4 != 0) {
        return -EINVAL;
    }

    tt_tlb_t* tlb = NULL;
    int ret = tt_tlb_alloc(dev, TT_TLB_SIZE_2M, TT_MMIO_CACHE_MODE_UC, &tlb);
    if (ret != 0) {
        return ret;
    }

    uint64_t aligned_addr = addr & ~((uint64_t)tlb->size - 1);
    ret = tt_tlb_map_unicast(dev, tlb, x, y, aligned_addr);
    if (ret != 0) {
        tt_tlb_free(dev, tlb);
        return ret;
    }

    uint64_t offset = addr & (tlb->size - 1);
    *(volatile uint32_t*)((uint8_t*)tlb->mmio + offset) = value;

    tt_tlb_free(dev, tlb);
    return 0;
}

int tt_noc_read(tt_device_t* dev, uint8_t x, uint8_t y, uint64_t addr, void* dst, size_t len) {
    uint8_t* dst_ptr = (uint8_t*)dst;
    tt_tlb_t* tlb = NULL;
    int ret;

    if (dst == NULL || addr % 4 != 0 || len % 4 != 0) {
        return -EINVAL;
    }

    ret = tt_tlb_alloc(dev, TT_TLB_SIZE_2M, TT_MMIO_CACHE_MODE_WC, &tlb);
    if (ret != 0) {
        return ret;
    }

    while (len > 0) {
        uint64_t aligned_addr = addr & ~((uint64_t)tlb->size - 1);
        uint64_t offset = addr & (tlb->size - 1);
        size_t chunk_size = MIN(len, tlb->size - offset);
        uint8_t* src_ptr = (uint8_t*)tlb->mmio + offset;

        ret = tt_tlb_map_unicast(dev, tlb, x, y, aligned_addr);
        if (ret != 0) {
            tt_tlb_free(dev, tlb);
            return ret;
        }

        memcpy(dst_ptr, src_ptr, chunk_size);

        dst_ptr += chunk_size;
        len -= chunk_size;
        addr += chunk_size;
    }

    tt_tlb_free(dev, tlb);
    return 0;
}

int tt_noc_write(tt_device_t* dev, uint8_t x, uint8_t y, uint64_t addr, const void* src, size_t len) {
    const uint8_t* src_ptr = (const uint8_t*)src;
    tt_tlb_t* tlb = NULL;
    int ret;

    if (src == NULL || addr % 4 != 0 || len % 4 != 0) {
        return -EINVAL;
    }

    ret = tt_tlb_alloc(dev, TT_TLB_SIZE_2M, TT_MMIO_CACHE_MODE_WC, &tlb);
    if (ret != 0) {
        return ret;
    }

    while (len > 0) {
        uint64_t aligned_addr = addr & ~((uint64_t)tlb->size - 1);
        uint64_t offset = addr & (tlb->size - 1);
        size_t chunk_size = MIN(len, tlb->size - offset);
        uint8_t* dst_ptr = (uint8_t*)tlb->mmio + offset;

        ret = tt_tlb_map_unicast(dev, tlb, x, y, aligned_addr);
        if (ret != 0) {
            tt_tlb_free(dev, tlb);
            return ret;
        }

        memcpy(dst_ptr, src_ptr, chunk_size);

        src_ptr += chunk_size;
        len -= chunk_size;
        addr += chunk_size;
    }

    tt_tlb_free(dev, tlb);
    return 0;
}

/* --- not yet supported by tt-wind --------------------------------------- */

/* Everything below returns -ENOSYS because the driver has no backing
 * facility yet: DMA mapping / page pinning / hugepage-style DMA buffers need
 * driver-owned common-buffer DMA and NOC-visible IOVA programming, dma-buf
 * export is a Linux-only kernel object, and per-handle power-state voting
 * (TT_POWER_FLAG_*) has no tt-wind ioctl - the driver unconditionally sends
 * the power-up messages at start instead. */

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

/* --- device reset ------------------------------------------------------- */

/* Backed by tt-wind's split arm/recover reset (driver 100.3.4.0+). tt-wind
 * implements exactly one reset kind - tt-kmd's Blackhole ASIC reset with
 * config-space save/restore and a firmware power-up afterwards - so the Linux
 * reset-flag values that name a whole-device reset (RESTORE_STATE, USER_RESET,
 * ASIC_RESET) all map to it. The remaining values (RESET_PCIE_LINK,
 * CONFIG_WRITE, ASIC_DMC_RESET, POST_RESET) have no tt-wind equivalent and
 * return -ENOSYS.
 *
 * IOCTL_TTWIND_RESET_DEVICE only ARMS the chip's self-reset and returns
 * immediately (the driver never sleeps or touches MMIO while the device may
 * be off the bus); recovery is IOCTL_TTWIND_POST_RESET (driver 100.3.5.0
 * semantics): after a ~2 s grace period for the DBI timer to fire (matching
 * tt-kmd's userspace warm_reset.cpp), this function polls it about every
 * 100 ms until the device is back, ~15 s bound. To the caller the whole
 * arm+recover flow is still one synchronous tt_device_reset() call,
 * matching the Linux backend's semantics.
 *
 * The driver refuses the arm with -EBUSY while any user mapping of device
 * memory exists on any handle (it cannot revoke user page tables yet); the
 * caller must unmap/free all BAR and TLB mappings first. */
int tt_device_reset(tt_device_t* dev, uint32_t reset_flags) {
    if (dev == NULL) {
        return -EINVAL;
    }

    switch (reset_flags) {
        case 0u: /* TENSTORRENT_RESET_DEVICE_RESTORE_STATE */
        case 3u: /* TENSTORRENT_RESET_DEVICE_USER_RESET */
        case 4u: /* TENSTORRENT_RESET_DEVICE_ASIC_RESET */
            break;
        default:
            return -ENOSYS;
    }

    /* Arm the reset. Fails without disturbing the device (e.g. -EBUSY while
     * user mappings exist, -ENODEV if the device is already off the bus). */
    TTWIND_RESET_DEVICE_IN reset_in;
    memset(&reset_in, 0, sizeof(reset_in));
    int err = ttwind_ioctl(dev->handle, IOCTL_TTWIND_RESET_DEVICE, &reset_in, sizeof(reset_in), NULL, 0);
    if (err != 0) {
        return err;
    }

    /* Give the DBI interface timer a grace period to fire before the first
     * POST_RESET poll, matching tt-kmd's userspace (warm_reset.cpp sleeps
     * ~2 s between trigger and recovery). */
    Sleep(2000);

    /* Poll POST_RESET until the device returns and recovery completes. Every
     * failure status is retryable within the budget:
     *  - ERROR_BAD_UNIT (STATUS_DEVICE_DOES_NOT_EXIST): device not back on
     *    the bus yet.
     *  - ERROR_BUSY (STATUS_DEVICE_BUSY): reset pending - the device answers
     *    config cycles but the reset marker is still set (the DBI timer has
     *    not fired yet). NOTE the asymmetry: ERROR_BUSY from RESET_DEVICE
     *    above still means user-mappings-exist and stays a terminal -EBUSY;
     *    from POST_RESET it means keep polling (driver 100.3.5.0).
     *  - The restore/MMIO failures likewise just get retried.
     * Outcome mapping:
     *   recovered                            ->  0
     *   chip ignored the reset trigger       -> -ENXIO (the FULL budget
     *       expired with every poll reporting the marker still set - per
     *       the driver, that terminal diagnosis is the caller's to make)
     *   device never came back within budget -> -ETIMEDOUT */
    TTWIND_POST_RESET_IN post_in;
    memset(&post_in, 0, sizeof(post_in));
    BOOL busy_throughout = TRUE;
    for (DWORD waited = 0;; waited += 100) {
        DWORD returned = 0;
        if (DeviceIoControl(
                dev->handle, IOCTL_TTWIND_POST_RESET, &post_in, sizeof(post_in), NULL, 0, &returned, NULL)) {
            return 0;
        }
        if (GetLastError() != ERROR_BUSY) {
            busy_throughout = FALSE;
        }
        if (waited >= 15000) {
            return busy_throughout ? -ENXIO : -ETIMEDOUT;
        }
        Sleep(100);
    }
}

/* --- resource locks ------------------------------------------------------ */

/* The 64 advisory per-device resource locks are implemented with named kernel
 * mutexes rather than a driver ioctl (tt-wind has none yet):
 *
 *  - Namespace: the per-session "Local\" namespace, matching UMD's
 *    RobustMutex, which guards the same class of cross-process resources.
 *    All processes coordinating over a device normally run in one logon
 *    session; system-wide exclusion across sessions/services would need
 *    "Global\" and is not needed today.
 *  - Key: the device's PCI location (domain/bus/device/function) plus the
 *    slot index. The PCI location identifies the physical device no matter
 *    whether it was opened by ordinal or by full interface path (an ordinal
 *    is just a position in the sorted interface list).
 *  - Crash recovery: a mutex whose owner died is acquired with
 *    WAIT_ABANDONED; that counts as a successful (recovered) acquire, the
 *    same policy as RobustMutex.
 *
 * Caveat vs the Linux driver locks: Win32 mutex ownership is per-THREAD, not
 * per device handle, and is recursive on the owning thread. Release (and a
 * close that still holds locks) must happen on the acquiring thread; if it
 * does not, the lock stays held until the owning thread exits. Acquire
 * semantics are made non-recursive by the in-process lock registry (defined
 * near the top of this file, since tt_device_close also consults it). */

static int lock_get_mutex(tt_device_t* dev, uint8_t index, HANDLE* out_mutex) {
    if (dev->lock_handles[index] == NULL) {
        TTWIND_DEVICE_INFO_OUT info;
        int err = get_device_info(dev, &info);
        if (err != 0) {
            return err;
        }

        wchar_t name[LOCK_NAME_CHARS];
        swprintf(
            name,
            ARRAYSIZE(name),
            L"Local\\ttwind-lock-%04x:%02x:%02x.%x-slot%u",
            info.PciDomain,
            info.Bus,
            info.Device,
            info.Function,
            (unsigned)index);

        lock_registry_enter();
        int slot = lock_registry_find_or_add(name);
        lock_registry_leave();
        if (slot < 0) {
            return -ENOMEM;
        }

        HANDLE h = CreateMutexW(NULL, FALSE, name);
        if (h == NULL) {
            return last_err();
        }
        dev->lock_handles[index] = h;
        dev->lock_registry_slot_plus1[index] = (unsigned)slot + 1;
    }
    *out_mutex = dev->lock_handles[index];
    return 0;
}

int tt_lock_acquire(tt_device_t* dev, uint8_t index, int* out_acquired) {
    if (out_acquired != NULL) {
        *out_acquired = 0;
    }
    if (dev == NULL || index >= TT_RESOURCE_LOCK_COUNT || out_acquired == NULL) {
        return -EINVAL;
    }

    HANDLE mutex = NULL;
    int err = lock_get_mutex(dev, index, &mutex);
    if (err != 0) {
        return err;
    }

    /* The registry check makes the acquire non-recursive: while any handle in
     * this process holds the slot, every further acquire fails, including one
     * from the holding handle -- which a bare zero-timeout wait on the owning
     * thread would wrongly grant. The wait itself stays inside the critical
     * section so the held flag and the mutex cannot go out of step. */
    lock_registry_enter();
    lock_registry_entry* entry = &g_lock_registry[dev->lock_registry_slot_plus1[index] - 1];
    if (entry->held) {
        lock_registry_leave();
        *out_acquired = 0;
        return 0;
    }

    switch (WaitForSingleObject(mutex, 0)) {
        case WAIT_OBJECT_0:
        case WAIT_ABANDONED: /* previous owner died; lock recovered */
            entry->held = 1;
            dev->lock_held_count[index]++;
            lock_registry_leave();
            *out_acquired = 1;
            return 0;
        case WAIT_TIMEOUT:
            lock_registry_leave();
            *out_acquired = 0;
            return 0;
        default:
            lock_registry_leave();
            return last_err();
    }
}

int tt_lock_release(tt_device_t* dev, uint8_t index, int* out_was_held) {
    if (out_was_held != NULL) {
        *out_was_held = 0;
    }
    if (dev == NULL || index >= TT_RESOURCE_LOCK_COUNT) {
        return -EINVAL;
    }

    /* Not held by this handle: report a double release, like the driver. */
    if (dev->lock_handles[index] == NULL || dev->lock_held_count[index] == 0) {
        return 0;
    }

    lock_registry_enter();
    if (!ReleaseMutex(dev->lock_handles[index])) {
        lock_registry_leave();
        /* ERROR_NOT_OWNER: held, but not by the calling thread (see the
         * thread-affinity caveat above). */
        return (GetLastError() == ERROR_NOT_OWNER) ? -EPERM : last_err();
    }

    dev->lock_held_count[index]--;
    if (dev->lock_held_count[index] == 0) {
        g_lock_registry[dev->lock_registry_slot_plus1[index] - 1].held = 0;
    }
    lock_registry_leave();
    if (out_was_held != NULL) {
        *out_was_held = 1;
    }
    return 0;
}

int tt_lock_test(tt_device_t* dev, uint8_t index, uint32_t* out_state) {
    if (out_state != NULL) {
        *out_state = 0;
    }
    if (dev == NULL || index >= TT_RESOURCE_LOCK_COUNT || out_state == NULL) {
        return -EINVAL;
    }

    if (dev->lock_held_count[index] > 0) {
        *out_state = TT_LOCK_STATE_HELD_BY_SELF | TT_LOCK_STATE_HELD_BY_ANY;
        return 0;
    }

    HANDLE mutex = NULL;
    int err = lock_get_mutex(dev, index, &mutex);
    if (err != 0) {
        return err;
    }

    /* A holder inside this process is visible in the registry; the probe
     * below would wrongly succeed for it on the owning thread (recursive
     * mutex), so answer from the registry first. */
    lock_registry_enter();
    if (g_lock_registry[dev->lock_registry_slot_plus1[index] - 1].held) {
        lock_registry_leave();
        *out_state = TT_LOCK_STATE_HELD_BY_ANY;
        return 0;
    }

    /* No handle in this process holds the slot (and none can take it while
     * the critical section is held), so probe with a zero-timeout acquire; on
     * success nobody held it, so undo the probe immediately. Inherently racy
     * against other processes, like the driver's test. */
    switch (WaitForSingleObject(mutex, 0)) {
        case WAIT_OBJECT_0:
        case WAIT_ABANDONED:
            ReleaseMutex(mutex);
            lock_registry_leave();
            *out_state = 0;
            return 0;
        case WAIT_TIMEOUT:
            lock_registry_leave();
            *out_state = TT_LOCK_STATE_HELD_BY_ANY;
            return 0;
        default:
            lock_registry_leave();
            return last_err();
    }
}
