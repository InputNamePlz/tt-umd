// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Forced-include (/FI) prelude for the tt-umd test binaries on Windows (INTERFACE of test_common).
// Supplies the POSIX calls that the cross-platform tests use and the MSVC CRT lacks.

#pragma once

#include <cerrno>
#include <cstdlib>

#include "tt_umd_windows_prelude.h"

// setenv/unsetenv on top of _putenv_s, which getenv() observes. Windows cannot hold an environment
// variable with an empty value: setenv(name, "", 1) removes the variable instead (see
// windows/tests/gtest_exclude.txt for the test that depends on the difference).
inline int setenv(const char* name, const char* value, int overwrite) {
    if (overwrite == 0 && std::getenv(name) != nullptr) {
        return 0;
    }
    return _putenv_s(name, value) == 0 ? 0 : -1;
}

inline int unsetenv(const char* name) { return _putenv_s(name, "") == 0 ? 0 : -1; }

// fcntl() only for the F_GETFD validity checks on exported dma-buf descriptors in
// tests/unified/test_tlb.cpp. dma-buf export does not exist on Windows (those tests skip before
// reaching it), so the call always reports failure.
#ifndef F_GETFD
#define F_GETFD 1
#endif

inline int fcntl(int /*fd*/, int /*cmd*/, ...) {
    errno = ENOSYS;
    return -1;
}
