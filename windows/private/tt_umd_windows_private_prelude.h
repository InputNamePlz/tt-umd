// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Forced-include (/FI) prelude for UMD's own C++ sources (PRIVATE on the tt-umd-utils, tt-umd-base
// and tt-umd targets) and, through windows/tests/test_prelude.h, for the test binaries. Never
// applied to consumers: see windows/include/tt_umd_windows_prelude.h for the part they get.

#pragma once

#include "../include/tt_umd_windows_prelude.h"

// windows.h first, so that every later inclusion (spdlog, asio, the Windows backend) is a no-op
// and the macro clean-up below sticks. NOMINMAX / WIN32_LEAN_AND_MEAN come from the build.
#include <windows.h>

// Object-like macros from <windows.h> that collide with upstream enumerators. None of them is
// used after <windows.h> itself has been processed:
//  - IGNORE (winbase.h, a Win16 relic): TopologyDiscoveryOptions::Action::IGNORE.
//  - STRICT (windef.h, only selects the handle typedefs while windows.h is read): IoOrdering::STRICT.
//  - ERROR (wingdi.h, a GDI region-function result): DmaTransferState::ERROR.
#undef IGNORE
#undef STRICT
#undef ERROR
