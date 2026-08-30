// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(_MSC_VER) && !defined(__clang__)

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

#else  // !_MSC_VER

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

namespace tt_driver_atomics {

#if defined(__x86_64__) || defined(__i386__)
// Store-Any barrier.
static inline __attribute__((always_inline)) void sfence() { _mm_sfence(); }

// Load-Any barrier.
static inline __attribute__((always_inline)) void lfence() { _mm_lfence(); }

// Any-Any barrier.
static inline __attribute__((always_inline)) void mfence() { _mm_mfence(); }

#elif defined(__ARM_ARCH)

static inline __attribute__((always_inline)) void sfence() {
    // Full memory barrier (full system). ARM does not have a Store-Any barrier.
    // https://developer.arm.com/documentation/100941/0101/Barriers
    asm volatile("DMB SY" : : : "memory");
}

static inline __attribute__((always_inline)) void lfence() {
    // Load-Any barrier (full system)
    // https://developer.arm.com/documentation/100941/0101/Barriers
    asm volatile("DMB LD" : : : "memory");
}

static inline __attribute__((always_inline)) void mfence() {
    // Full memory barrier (full system).
    // https://developer.arm.com/documentation/100941/0101/Barriers
    asm volatile("DMB SY" : : : "memory");
}

#elif defined(__riscv)

static inline __attribute__((always_inline)) void sfence() { asm volatile("fence ow, ow" : : : "memory"); }

static inline __attribute__((always_inline)) void lfence() { asm volatile("fence ir, ir" : : : "memory"); }

static inline __attribute__((always_inline)) void mfence() { asm volatile("fence iorw, iorw" : : : "memory"); }

#else
#error "Unsupported architecture"
#endif

}  // namespace tt_driver_atomics

#endif  // _MSC_VER
