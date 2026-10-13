// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Windows stand-in for <dlfcn.h>, used by tt-umd/jtag/jtag.hpp (public, for RTLD_LAZY) and
// tt-umd/src/jtag/jtag.cpp. Backed by LoadLibrary/GetProcAddress in posix_compat_windows.cpp.
// Supported: dlopen(path, flags) with flags ignored, dlsym, dlclose, dlerror (per-thread, cleared
// by reading it). Not supported: RTLD_DEFAULT/RTLD_NEXT pseudo-handles, dlopen(nullptr).

#pragma once

#define RTLD_LAZY 0x0001
#define RTLD_NOW 0x0002
#define RTLD_LOCAL 0x0000
#define RTLD_GLOBAL 0x0100

void* tt_umd_win_dlopen(const char* file, int flags);
void* tt_umd_win_dlsym(void* handle, const char* name);
int tt_umd_win_dlclose(void* handle);
const char* tt_umd_win_dlerror();

inline void* dlopen(const char* file, int flags) { return tt_umd_win_dlopen(file, flags); }

inline void* dlsym(void* handle, const char* name) { return tt_umd_win_dlsym(handle, name); }

inline int dlclose(void* handle) { return tt_umd_win_dlclose(handle); }

inline const char* dlerror() { return tt_umd_win_dlerror(); }
