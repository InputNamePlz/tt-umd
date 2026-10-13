// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Windows stand-in for <unistd.h>, visible to tt-umd's own sources and tests only (not to
// consumers). It covers exactly what those files use; anything else is a compile error, so new
// POSIX dependencies upstream show up at build time.
//
// Supported: sysconf(_SC_PAGESIZE), getpagesize(), gethostname() (computer name via
// GetComputerNameA), ssize_t, PATH_MAX, and the CRT's POSIX names (close, read, write, getpid,
// _exit). sysconf() with any other name aborts.

#pragma once

#include <io.h>
#include <process.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef intptr_t ssize_t;

#ifndef PATH_MAX
#define PATH_MAX 260
#endif

#define _SC_PAGESIZE 30
#define _SC_PAGE_SIZE _SC_PAGESIZE

long tt_umd_win_sysconf(int name);
int tt_umd_win_gethostname(char* name, size_t len);

inline long sysconf(int name) { return tt_umd_win_sysconf(name); }

inline int getpagesize() { return static_cast<int>(tt_umd_win_sysconf(_SC_PAGESIZE)); }

inline int gethostname(char* name, size_t len) { return tt_umd_win_gethostname(name, len); }
