// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Windows stand-in for the Itanium C++ ABI <cxxabi.h>, used by tt-umd/utils/error_detail.hpp
// (public). MSVC names are not Itanium-mangled and dbghelp already returns undecorated names, so
// demangling always reports "invalid mangled name" (-2) and callers keep the name as is.

#pragma once

#include <cstddef>

namespace abi {

inline char* __cxa_demangle(const char* /*mangled_name*/, char* /*output_buffer*/, size_t* /*length*/, int* status) {
    if (status != nullptr) {
        *status = -2;
    }
    return nullptr;
}

}  // namespace abi
