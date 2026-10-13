// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Stand-in source for test_utils_tests on Windows. Upstream's only test there exercises
// MultiProcessPipe (fork/pipe), which has no Windows equivalent; the target is kept so that the
// umd_tests aggregate target still resolves. main() comes from gtest_main.
