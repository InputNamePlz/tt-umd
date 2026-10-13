// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Backend of windows/compat/public/execinfo.h: the stack capture behind UmdException
// (error::get_stacktrace() in error_detail.hpp calls backtrace() and backtrace_symbols()).

#include <windows.h>

#include <dbghelp.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "execinfo.h"

#pragma comment(lib, "dbghelp.lib")

namespace {

// SymInitialize is process-wide and must run once; every Sym* call afterwards is serialized by the
// same mutex because dbghelp is not thread-safe.
std::mutex& sym_mutex() {
    static std::mutex mutex;
    return mutex;
}

bool sym_initialized() {
    static const bool initialized = SymInitialize(GetCurrentProcess(), nullptr, TRUE) != 0;
    return initialized;
}

std::string module_base_name(HMODULE module) {
    char path[MAX_PATH] = {};
    if (module == nullptr || GetModuleFileNameA(module, path, MAX_PATH) == 0) {
        return "?";
    }
    const char* base = path;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '\\' || *p == '/') {
            base = p + 1;
        }
    }
    return base;
}

std::string describe_frame(void* address) {
    char buffer[256];
    HMODULE module = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<const char*>(address),
        &module);

    if (sym_initialized()) {
        alignas(SYMBOL_INFO) char symbol_storage[sizeof(SYMBOL_INFO) + 512] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 511;
        DWORD64 displacement = 0;
        if (SymFromAddr(GetCurrentProcess(), reinterpret_cast<DWORD64>(address), &displacement, symbol)) {
            snprintf(
                buffer,
                sizeof(buffer),
                "%s!%s+0x%llx",
                module_base_name(module).c_str(),
                symbol->Name,
                static_cast<unsigned long long>(displacement));
            return buffer;
        }
    }

    // No symbols: report a module-relative address, which is stable across ASLR and can be
    // resolved offline against the matching PDB.
    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    const uintptr_t addr = reinterpret_cast<uintptr_t>(address);
    snprintf(
        buffer,
        sizeof(buffer),
        "%s+0x%llx",
        module_base_name(module).c_str(),
        static_cast<unsigned long long>(base != 0 ? addr - base : addr));
    return buffer;
}

}  // namespace

int tt_umd_win_backtrace(void** buffer, int size) {
    if (buffer == nullptr || size <= 0) {
        return 0;
    }
    // Skip this frame, so that like glibc's backtrace() the first entry is the caller's frame.
    // CaptureStackBackTrace takes at most 62 frames on older Windows versions; capture in chunks.
    int captured = 0;
    while (captured < size) {
        const ULONG chunk = static_cast<ULONG>(std::min(size - captured, 62));
        const USHORT got = CaptureStackBackTrace(1 + static_cast<ULONG>(captured), chunk, buffer + captured, nullptr);
        captured += got;
        if (got < chunk) {
            break;
        }
    }
    return captured;
}

char** tt_umd_win_backtrace_symbols(void* const* buffer, int size) {
    if (buffer == nullptr || size <= 0) {
        return nullptr;
    }
    std::vector<std::string> frames;
    frames.reserve(size);
    {
        std::lock_guard<std::mutex> lock(sym_mutex());
        for (int i = 0; i < size; ++i) {
            frames.push_back(describe_frame(buffer[i]));
        }
    }

    // One allocation holding the pointer array followed by the strings, released with a single
    // free() by the caller (glibc contract).
    size_t total = sizeof(char*) * static_cast<size_t>(size);
    for (const std::string& frame : frames) {
        total += frame.size() + 1;
    }
    char** result = static_cast<char**>(std::malloc(total));
    if (result == nullptr) {
        return nullptr;
    }
    char* strings = reinterpret_cast<char*>(result + size);
    for (int i = 0; i < size; ++i) {
        result[i] = strings;
        std::memcpy(strings, frames[i].c_str(), frames[i].size() + 1);
        strings += frames[i].size() + 1;
    }
    return result;
}
