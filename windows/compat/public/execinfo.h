// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Windows stand-in for glibc's <execinfo.h>, used by tt-umd/utils/error_detail.hpp (public) to
// attach stack traces to UmdException. Backed by CaptureStackBackTrace and dbghelp in
// windows/tt-umd/src/utils/stacktrace_windows.cpp. backtrace_symbols() returns one malloc'd block
// that the caller releases with free(), as with glibc. Entries read "module!symbol+0xoffset", or
// "module+0xoffset" when no PDB is available.

#pragma once

int tt_umd_win_backtrace(void** buffer, int size);
char** tt_umd_win_backtrace_symbols(void* const* buffer, int size);

inline int backtrace(void** buffer, int size) { return tt_umd_win_backtrace(buffer, size); }

inline char** backtrace_symbols(void* const* buffer, int size) { return tt_umd_win_backtrace_symbols(buffer, size); }
