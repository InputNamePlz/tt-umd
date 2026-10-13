// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Forced-include (/FI) prelude for every C++ translation unit of tt-umd and of its consumers on
// Windows (PUBLIC on the tt-umd target). It supplies the few POSIX/GCC spellings that tt-umd's
// public headers rely on, so that those headers compile unmodified with MSVC.
//
// Keep this minimal: everything here leaks into consumers. File-specific needs go into per-source
// options set by windows/cmake/umd_windows_targets.cmake instead.

#pragma once

#ifndef _WIN32
#error "tt_umd_windows_prelude.h is only meant for Windows builds."
#endif

// windows.h first, so that every later inclusion (spdlog, asio, the Windows backend) is a no-op
// and the macro clean-up below sticks. NOMINMAX / WIN32_LEAN_AND_MEAN come from the build.
#include <windows.h>

// winbase.h defines IGNORE as an object-like macro (a Win16 relic), which would mangle the
// TopologyDiscoveryOptions::Action::IGNORE enumerator.
#undef IGNORE

// POSIX process/thread id type used in public lock APIs (probe_lock() etc.). MSVC's
// <sys/types.h> does not define it.
typedef int pid_t;

// GCC builtin used in a static_assert in robust_mutex.hpp. Every naturally aligned type up to
// pointer size is lock-free on the architectures Windows supports (x64, ARM64).
#if !defined(__clang__) && !defined(__atomic_always_lock_free)
#define __atomic_always_lock_free(size, ptr) ((size) <= sizeof(void*))
#endif
