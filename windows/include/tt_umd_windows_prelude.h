// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Forced-include (/FI) prelude for every C++ translation unit that consumes tt-umd on Windows
// (INTERFACE on the tt-umd target). It supplies the few POSIX/GCC spellings that tt-umd's public
// headers rely on, so that those headers compile unmodified with MSVC.
//
// Everything here leaks into every consumer translation unit, so it must stay minimal and must not
// include any header: in particular not <windows.h>, whose object-like macros (IN, OUT, ERROR,
// IGNORE, min, max, ...) break ordinary consumer code. What only tt-umd's own sources and tests
// need goes into windows/private/tt_umd_windows_private_prelude.h instead.

#pragma once

#ifndef _WIN32
#error "tt_umd_windows_prelude.h is only meant for Windows builds."
#endif

// POSIX process/thread id type used in public lock APIs (probe_lock() etc.). MSVC's
// <sys/types.h> does not define it.
typedef int pid_t;

// GCC builtin used in a static_assert in robust_mutex.hpp. Every naturally aligned type up to
// pointer size is lock-free on the architectures Windows supports (x64, ARM64).
#if !defined(__clang__) && !defined(__atomic_always_lock_free)
#define __atomic_always_lock_free(size, ptr) ((size) <= sizeof(void*))
#endif
