// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Windows shadow of tt-umd/api/tt-umd/driver_atomics.hpp. This directory is placed BEFORE tt-umd's
// api/ directory on the include path, so "tt-umd/driver_atomics.hpp" resolves here on Windows.
// The upstream header selects its implementation with GCC macros (__x86_64__, __ARM_ARCH) and GNU
// inline asm, neither of which MSVC provides, and ends in #error.
//
// Keep the function set identical to upstream. windows/cmake/umd_windows_targets.cmake records a
// hash of the upstream header this copy was synced against and fails the configure when upstream
// changes it.

#pragma once

#include <intrin.h>

namespace tt_driver_atomics {

#if defined(_M_X64) || defined(_M_IX86)

// Store-Any barrier.
static __forceinline void sfence() { _mm_sfence(); }

// Load-Any barrier.
static __forceinline void lfence() { _mm_lfence(); }

// Any-Any barrier.
static __forceinline void mfence() { _mm_mfence(); }

#elif defined(_M_ARM64)

// Full memory barrier (full system). ARM does not have a Store-Any barrier.
static __forceinline void sfence() { __dmb(_ARM64_BARRIER_SY); }

// Load-Any barrier (full system).
static __forceinline void lfence() { __dmb(_ARM64_BARRIER_LD); }

// Full memory barrier (full system).
static __forceinline void mfence() { __dmb(_ARM64_BARRIER_SY); }

#else
#error "Unsupported architecture"
#endif

}  // namespace tt_driver_atomics
