// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Windows stand-in for <sys/wait.h>, for the tests only. tests/test_utils/process_utils.hpp
// includes it for its fork()-based helpers; tests built on Windows include that header for other
// helpers (read_rss_kib()) and never reach the process ones, which abort if called.

#pragma once

#include <cstdio>
#include <cstdlib>

#ifndef SIGKILL
#define SIGKILL 9
#endif

#define WNOHANG 1

[[noreturn]] inline void tt_umd_win_test_process_unsupported(const char* what) {
    std::fprintf(stderr, "%s is not available on Windows\n", what);
    std::abort();
}

inline pid_t waitpid(pid_t /*pid*/, int* /*status*/, int /*options*/) { tt_umd_win_test_process_unsupported("waitpid()"); }

inline int kill(pid_t /*pid*/, int /*sig*/) { tt_umd_win_test_process_unsupported("kill()"); }
