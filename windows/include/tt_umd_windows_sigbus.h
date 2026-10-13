// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Forced-include for tt-umd/src/pcie/silicon_tlb_window.cpp only (see umd_windows_targets.cmake).
//
// SiliconTlbWindow guards device accesses with a SIGBUS handler plus sigsetjmp/siglongjmp. Windows
// has no SIGBUS; a failed MMIO access surfaces as a structured exception instead. Until a
// Windows fault guard exists, this header makes the POSIX spellings compile with "no fault
// interception" semantics:
//  - sigaction() accepts the request and installs nothing, so the handler never runs and
//    siglongjmp() is never reached; sigsetjmp() always takes the direct path.
//  - Restoring the default disposition with signal(SIGBUS, SIG_DFL) is not supported: SIGBUS is
//    not a CRT signal, so the CRT's invalid-parameter handler terminates the process. Nothing in
//    the Windows build calls set_sigbus_safe_handler(false).

#pragma once

#include <csetjmp>
#include <csignal>

#ifndef SIGBUS
#define SIGBUS 10
#endif

#ifndef SA_NODEFER
#define SA_NODEFER 0x40000000
#endif

typedef jmp_buf sigjmp_buf;
#define sigsetjmp(env, savemask) setjmp(env)
#define siglongjmp(env, val) longjmp(env, val)

typedef int sigset_t;

struct sigaction {
    void (*sa_handler)(int);
    sigset_t sa_mask;
    int sa_flags;
};

inline int sigemptyset(sigset_t* set) {
    *set = 0;
    return 0;
}

inline int sigaction(int /*signum*/, const struct sigaction* /*act*/, struct sigaction* oldact) {
    if (oldact != nullptr) {
        *oldact = {};
    }
    return 0;
}
