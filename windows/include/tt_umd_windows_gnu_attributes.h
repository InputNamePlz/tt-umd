// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Forced-include for the few tt-umd sources that use GCC attributes MSVC does not understand and
// that carry no meaning for the Windows build: noinline (tlb_window.cpp, a code-layout workaround
// for a GCC/x86 issue) and tls_model (silicon_tlb_window.cpp). It is applied per source file by
// windows/cmake/umd_windows_targets.cmake, so an attribute anywhere else stays a compile error.

#pragma once

#define __attribute__(x)
