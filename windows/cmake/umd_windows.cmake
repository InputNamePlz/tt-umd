# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
#
# SPDX-License-Identifier: Apache-2.0

# Windows overlay for tt-umd. Included by CMake right after project(umd) (CMAKE_PROJECT_umd_INCLUDE,
# set by windows/cmake/msvc-cache.cmake), so it adapts the upstream build without editing any
# upstream CMakeLists.txt:
#   1. now, before any target exists: directory-level MSVC settings, inherited by every target;
#   2. at the end of the directory that called project(umd), when all targets exist:
#      umd_windows_targets.cmake swaps Linux-only sources for Windows ones and fixes up targets.

include_guard(GLOBAL)

if(NOT MSVC)
    message(FATAL_ERROR "The tt-umd Windows overlay (${CMAKE_CURRENT_LIST_FILE}) requires the MSVC toolchain.")
endif()

get_filename_component(TT_WIN_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(TT_WIN_ROOT "${TT_WIN_ROOT}" CACHE INTERNAL "Root of the tt-umd Windows overlay")

# Everything below is directory-scoped: this file runs in the scope of the directory that called
# project(umd), so it reaches UMD and its subdirectories but never the targets of a parent project
# that adds UMD with add_subdirectory(). No cache entries, global properties or CMAKE_*_FLAGS are
# changed here.

# /W3 instead of the GCC warning flags upstream adds (those are stripped per target later).
add_compile_options(/W3)
# MSVC warnings differ from the GCC/Clang set upstream keeps clean, and upstream's root
# CMakeLists.txt turns warnings into errors unless CMAKE_COMPILE_WARNING_AS_ERROR is defined. A
# normal (directory) variable, so a parent project's own setting is left alone.
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
# windows.h's min/max macros break std::min/std::max; WIN32_LEAN_AND_MEAN keeps winsock.h out so
# that asio's winsock2.h does not conflict with it.
add_compile_definitions(
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

# Arguments of a deferred call are evaluated when it runs, hence TT_WIN_ROOT rather than
# CMAKE_CURRENT_LIST_DIR.
cmake_language(DEFER CALL include "${TT_WIN_ROOT}/cmake/umd_windows_targets.cmake")
