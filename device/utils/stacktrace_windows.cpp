// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Windows implementation of the stack capture behind UmdException, kept out of the public
// error_detail.hpp header so that windows.h and dbghelp.h do not leak into consumers.

#include <dbghelp.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

namespace tt::umd::error {

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

std::vector<std::string> capture_stacktrace_windows(uint32_t max_frames, uint32_t skip) {
    std::vector<std::string> stack_frames;
    if (max_frames == 0 || max_frames > 1024 || skip >= max_frames) {
        return stack_frames;
    }

    std::vector<void*> addresses(max_frames);
    // +1/-1: skip this function's own frame on top of the frames the caller asked to skip.
    const USHORT captured = CaptureStackBackTrace(skip + 1, max_frames - skip, addresses.data(), nullptr);

    std::lock_guard<std::mutex> lock(sym_mutex());
    for (USHORT i = 0; i < captured; ++i) {
        stack_frames.push_back(describe_frame(addresses[i]));
    }
    return stack_frames;
}

}  // namespace tt::umd::error
