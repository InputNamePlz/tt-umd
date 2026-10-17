// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Forced-include (/FI) prelude for tt-umd's own C++ sources (PRIVATE on the tt-umd target) and,
// through windows/tests/test_prelude.h, for the test binaries. Never applied to consumers: see
// windows/include/tt_umd_windows_prelude.h for the part they get.

#pragma once

#include "../include/tt_umd_windows_prelude.h"

// windows.h first, so that every later inclusion (spdlog, asio, the Windows backend) is a no-op
// and the macro clean-up below sticks. NOMINMAX / WIN32_LEAN_AND_MEAN come from the build.
#include <windows.h>

// winbase.h defines IGNORE as an object-like macro (a Win16 relic), which would mangle the
// TopologyDiscoveryOptions::Action::IGNORE enumerator.
#undef IGNORE
