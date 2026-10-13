# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
#
# SPDX-License-Identifier: Apache-2.0

# Initial cache for the Windows (MSVC) build of this fork. Use it instead of editing upstream CMake:
#
#   cmake -S . -B .build/windows-msvc -G Ninja -C windows/cmake/msvc-cache.cmake -DTT_UMD_BUILD_TESTS=ON
#   cmake --build .build/windows-msvc
#
# It turns on the Windows overlay (umd_windows.cmake), which CMake includes right after
# project(umd) through the standard CMAKE_PROJECT_<name>_INCLUDE hook. A parent project that adds
# UMD with add_subdirectory() only needs to set the same CMAKE_PROJECT_umd_INCLUDE variable.

set(CMAKE_PROJECT_umd_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/umd_windows.cmake" CACHE FILEPATH "tt-umd Windows overlay")

# With MSVC, project() defaults an empty CMAKE_BUILD_TYPE to Debug, which bypasses upstream's
# "Release unless specified" fallback in single-config generators.
set(CMAKE_BUILD_TYPE Release CACHE STRING "")
set(TT_UMD_BUILD_STATIC ON CACHE BOOL "")
# Not supported on Windows: the simulation transport is Unix domain sockets / fork, the tools and
# examples use POSIX APIs directly, and the Python bindings are not ported.
set(TT_UMD_BUILD_SIMULATION OFF CACHE BOOL "")
set(TT_UMD_BUILD_PYTHON OFF CACHE BOOL "")
set(TT_UMD_BUILD_TOOLS OFF CACHE BOOL "")
set(TT_UMD_BUILD_EXAMPLES OFF CACHE BOOL "")
set(TT_UMD_BUILD_JTAG OFF CACHE BOOL "")
set(TT_UMD_ENABLE_CLANG_TIDY OFF CACHE BOOL "")
# MSVC warnings differ from the GCC/Clang set upstream keeps clean.
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF CACHE BOOL "")
