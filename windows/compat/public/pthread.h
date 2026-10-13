// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Windows stand-in for <pthread.h>, used only by tt-umd/utils/robust_mutex.hpp (public), whose
// private members name pthread_mutex_t. The Windows RobustMutex
// (windows/tt-umd/src/utils/robust_mutex_windows.cpp) is a named kernel mutex and never touches
// these members, so only the type is provided; there are no pthread functions.

#pragma once

typedef struct {
    void* opaque[5];
} pthread_mutex_t;
