// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Windows stand-in for <sys/mman.h>, visible to tt-umd's own sources and tests only. It covers the
// anonymous-memory use in simulation_sysmem_manager.cpp and its test, backed by VirtualAlloc /
// VirtualFree in posix_compat_windows.cpp.
//
// Supported: mmap(nullptr, len, PROT_READ[|PROT_WRITE] or PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS
// [|MAP_POPULATE], -1, 0) (memory is committed and zero-filled; MAP_POPULATE is implied),
// munmap() of a whole mapping returned by mmap(), madvise() (advice is ignored).
// File-backed or shared mappings, fixed addresses and partial munmap() abort.

#pragma once

#include <stddef.h>
#include <stdint.h>

#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4

#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_POPULATE 0x08000

#define MAP_FAILED ((void*)(intptr_t)-1)

#define MADV_NORMAL 0
#define MADV_DONTNEED 4
#define MADV_HUGEPAGE 14
#define MADV_NOHUGEPAGE 15

void* tt_umd_win_mmap(void* addr, size_t length, int prot, int flags, int fd, int64_t offset);
int tt_umd_win_munmap(void* addr, size_t length);

inline void* mmap(void* addr, size_t length, int prot, int flags, int fd, int64_t offset) {
    return tt_umd_win_mmap(addr, length, prot, flags, fd, offset);
}

inline int munmap(void* addr, size_t length) { return tt_umd_win_munmap(addr, length); }

inline int madvise(void* /*addr*/, size_t /*length*/, int /*advice*/) { return 0; }
