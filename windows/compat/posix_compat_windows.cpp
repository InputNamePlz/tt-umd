// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Backends for the POSIX stand-in headers under windows/compat/ (dlfcn.h, unistd.h, sys/mman.h).
// The stack-trace backend for execinfo.h lives in windows/tt-umd/src/utils/stacktrace_windows.cpp.
// Each function supports exactly the subset its header documents and aborts on anything else, so
// an upstream change that starts relying on more of the POSIX API fails loudly instead of quietly
// misbehaving.

#include <windows.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "dlfcn.h"
#include "sys/mman.h"
#include "unistd.h"

namespace {

[[noreturn]] void unsupported(const char* what) {
    std::fprintf(stderr, "tt-umd Windows POSIX shim: unsupported use of %s\n", what);
    std::fflush(stderr);
    std::abort();
}

// dlerror() state: the message of the last failed dl* call on this thread, cleared once read.
thread_local std::string dl_last_error;
thread_local bool dl_error_pending = false;
thread_local std::string dl_returned_error;

void set_dl_error(const char* operation, const char* subject) {
    dl_last_error = std::string(operation) + "(" + (subject != nullptr ? subject : "") +
                    ") failed: Win32 error " + std::to_string(GetLastError());
    dl_error_pending = true;
}

}  // namespace

void* tt_umd_win_dlopen(const char* file, int /*flags*/) {
    if (file == nullptr) {
        unsupported("dlopen(nullptr)");
    }
    HMODULE module = LoadLibraryA(file);
    if (module == nullptr) {
        set_dl_error("LoadLibrary", file);
    }
    return module;
}

void* tt_umd_win_dlsym(void* handle, const char* name) {
    FARPROC symbol = GetProcAddress(static_cast<HMODULE>(handle), name);
    if (symbol == nullptr) {
        set_dl_error("GetProcAddress", name);
    }
    return reinterpret_cast<void*>(symbol);
}

int tt_umd_win_dlclose(void* handle) {
    if (!FreeLibrary(static_cast<HMODULE>(handle))) {
        set_dl_error("FreeLibrary", "");
        return -1;
    }
    return 0;
}

const char* tt_umd_win_dlerror() {
    if (!dl_error_pending) {
        return nullptr;
    }
    dl_error_pending = false;
    dl_returned_error = dl_last_error;
    return dl_returned_error.c_str();
}

long tt_umd_win_sysconf(int name) {
    if (name != _SC_PAGESIZE) {
        unsupported("sysconf() with a name other than _SC_PAGESIZE");
    }
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return static_cast<long>(info.dwPageSize);
}

int tt_umd_win_gethostname(char* name, size_t len) {
    // The NetBIOS computer name: no Winsock initialization needed, unlike Winsock's gethostname().
    DWORD size = static_cast<DWORD>(len);
    if (!GetComputerNameA(name, &size)) {
        errno = (GetLastError() == ERROR_BUFFER_OVERFLOW) ? ENAMETOOLONG : EFAULT;
        return -1;
    }
    return 0;
}

void* tt_umd_win_mmap(void* addr, size_t length, int prot, int flags, int fd, int64_t offset) {
    if (addr != nullptr || fd != -1 || offset != 0 || (flags & MAP_ANONYMOUS) == 0 || (flags & MAP_SHARED) != 0 ||
        (flags & MAP_FIXED) != 0 || (prot & PROT_EXEC) != 0) {
        unsupported("mmap() other than a private anonymous read/write mapping");
    }
    DWORD protection = PAGE_NOACCESS;
    if ((prot & PROT_WRITE) != 0) {
        protection = PAGE_READWRITE;
    } else if ((prot & PROT_READ) != 0) {
        protection = PAGE_READONLY;
    }
    void* mapping = VirtualAlloc(nullptr, length, MEM_RESERVE | MEM_COMMIT, protection);
    if (mapping == nullptr) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    return mapping;
}

int tt_umd_win_munmap(void* addr, size_t length) {
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(addr, &region, sizeof(region)) == 0 || region.AllocationBase != addr) {
        unsupported("munmap() of an address that is not the start of an mmap() mapping");
    }
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    const size_t page_size = info.dwPageSize;
    const size_t rounded_length = (length + page_size - 1) / page_size * page_size;
    if (rounded_length != region.RegionSize) {
        unsupported("partial munmap()");
    }
    if (!VirtualFree(addr, 0, MEM_RELEASE)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}
