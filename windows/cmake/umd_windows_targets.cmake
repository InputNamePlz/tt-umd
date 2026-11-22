# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
#
# SPDX-License-Identifier: Apache-2.0

# Second half of the Windows overlay (see umd_windows.cmake). Runs once the directory that called
# project(umd) has been fully processed, so every upstream target exists and can be adjusted.
#
# Drift guard: everything upstream-specific this file relies on (target names, source paths, link
# items, the contents of shadowed headers) is checked, and a mismatch is a FATAL_ERROR naming what
# to re-sync. After an upstream rebase the configure step fails loudly instead of building a
# silently wrong mix of Linux and Windows code.

set(_umd_root "${PROJECT_SOURCE_DIR}")
set(_win_root "${TT_WIN_ROOT}")

function(tt_win_fail)
    string(JOIN "" _message ${ARGN})
    message(FATAL_ERROR "tt-umd Windows overlay: ${_message}\n(see ${CMAKE_CURRENT_FUNCTION_LIST_FILE})")
endfunction()

function(tt_win_require_targets)
    foreach(_target IN LISTS ARGN)
        if(NOT TARGET ${_target})
            tt_win_fail("upstream target '${_target}' no longer exists.")
        endif()
    endforeach()
endfunction()

# Removes upstream sources from a target. A relative path is taken relative to the target's source
# dir; a source added from another directory (e.g. tt-umd-workload/tests adds to tests/ targets) is
# given as an absolute path. Each one must exist on disk and be listed on the target.
function(tt_win_remove_sources target)
    get_target_property(_source_dir ${target} SOURCE_DIR)
    get_target_property(_sources ${target} SOURCES)
    foreach(_relative IN LISTS ARGN)
        if(IS_ABSOLUTE "${_relative}")
            set(_absolute "${_relative}")
        else()
            set(_absolute "${_source_dir}/${_relative}")
        endif()
        if(NOT EXISTS "${_absolute}")
            tt_win_fail("upstream source '${_absolute}' no longer exists; update the overlay for target '${target}'.")
        endif()
        set(_found FALSE)
        foreach(_spelling IN ITEMS "${_relative}" "${_absolute}")
            list(FIND _sources "${_spelling}" _index)
            if(_index GREATER -1)
                list(REMOVE_AT _sources ${_index})
                set(_found TRUE)
            endif()
        endforeach()
        if(NOT _found)
            tt_win_fail("'${_relative}' is no longer a source of upstream target '${target}'.")
        endif()
    endforeach()
    set_property(TARGET ${target} PROPERTY SOURCES ${_sources})
endfunction()

# Removes link items (e.g. Linux system libraries) from a target. Each item must be present in
# LINK_LIBRARIES or INTERFACE_LINK_LIBRARIES, plain or wrapped as $<LINK_ONLY:item>.
function(tt_win_remove_link_items target)
    foreach(_item IN LISTS ARGN)
        set(_found FALSE)
        foreach(_property IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(_libraries ${target} ${_property})
            if(NOT _libraries)
                continue()
            endif()
            set(_kept "")
            foreach(_library IN LISTS _libraries)
                if(_library STREQUAL _item OR _library STREQUAL "$<LINK_ONLY:${_item}>")
                    set(_found TRUE)
                else()
                    list(APPEND _kept "${_library}")
                endif()
            endforeach()
            set_property(TARGET ${target} PROPERTY ${_property} ${_kept})
        endforeach()
        if(NOT _found)
            tt_win_fail("upstream target '${target}' no longer links '${_item}'.")
        endif()
    endforeach()
endfunction()

# Fails if an upstream file the overlay shadows with a Windows copy has changed since the copy was
# synced. The hash is taken over LF-normalized content, so it does not depend on core.autocrlf.
function(tt_win_check_shadowed upstream_relative shadow_relative expected_sha256)
    set(_upstream "${_umd_root}/${upstream_relative}")
    if(NOT EXISTS "${_upstream}")
        tt_win_fail(
            "upstream '${upstream_relative}' no longer exists; the Windows shadow copy "
            "windows/${shadow_relative} needs to move with it."
        )
    endif()
    file(READ "${_upstream}" _content)
    string(REPLACE "\r\n" "\n" _content "${_content}")
    string(SHA256 _actual "${_content}")
    if(NOT _actual STREQUAL "${expected_sha256}")
        tt_win_fail(
            "upstream ${upstream_relative} changed -- re-sync the Windows shadow copy windows/${shadow_relative} "
            "with it, then record the new hash ${_actual} (was ${expected_sha256})."
        )
    endif()
endfunction()

function(tt_win_collect_targets directory out_var)
    get_property(_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(_subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(_subdirectory IN LISTS _subdirectories)
        tt_win_collect_targets("${_subdirectory}" _nested)
        list(APPEND _targets ${_nested})
    endforeach()
    set(${out_var} ${_targets} PARENT_SCOPE)
endfunction()

# Upstream adds GCC/Clang warning flags (-Wall, -Wno-...) and -fno-omit-frame-pointer to every
# target from its root CMakeLists.txt, and warning flags to gtest. cl.exe would read -Wall as /Wall
# (every off-by-default warning) and ignore the rest with D9002 noise; x64 MSVC code has no
# frame-pointer option to map -fno-omit-frame-pointer to. -march=... is a generator expression that
# is false for AMD64, and -w is understood by cl.exe, so both stay.
# Only UMD's own directory tree is walked (PROJECT_SOURCE_DIR of project(umd), not
# CMAKE_SOURCE_DIR), so a parent project that adds UMD with add_subdirectory() keeps its flags.
function(tt_win_strip_gnu_warning_flags)
    tt_win_collect_targets("${_umd_root}" _targets)
    foreach(_target IN LISTS _targets)
        get_target_property(_imported ${_target} IMPORTED)
        if(_imported)
            continue()
        endif()
        foreach(_property IN ITEMS COMPILE_OPTIONS INTERFACE_COMPILE_OPTIONS)
            get_target_property(_options ${_target} ${_property})
            if(_options)
                set(_filtered ${_options})
                list(FILTER _filtered EXCLUDE REGEX "(^|:)(-W|-fno-omit-frame-pointer)")
                if(NOT _filtered STREQUAL _options)
                    set_property(TARGET ${_target} PROPERTY ${_property} ${_filtered})
                endif()
            endif()
        endforeach()
    endforeach()
endfunction()

####################################################################################################
# Shadowed upstream files
####################################################################################################

# windows/include/tt-umd/driver_atomics.hpp replaces tt-umd/api/tt-umd/driver_atomics.hpp.
tt_win_check_shadowed(
    "tt-umd/api/tt-umd/driver_atomics.hpp"
    "include/tt-umd/driver_atomics.hpp"
    "b1d74c365fab797c0f89d61632652766570f3531ffb4c72a7c1927ab31a0fa46"
)

####################################################################################################
# tt-kmd-lib: tt-wind driver backend instead of the Linux ioctl one
####################################################################################################

tt_win_require_targets(tt-kmd-lib)
tt_win_remove_sources(tt-kmd-lib src/tt_kmd_lib.c)
target_sources(tt-kmd-lib PRIVATE "${_win_root}/tt-kmd-lib/src/tt_kmd_lib_windows.c")
target_include_directories(tt-kmd-lib PUBLIC "$<BUILD_INTERFACE:${_win_root}/tt-kmd-lib/api>")
target_link_libraries(tt-kmd-lib PRIVATE cfgmgr32)

####################################################################################################
# Shared settings for the UMD libraries
####################################################################################################

# Applied to tt-umd-utils, tt-umd-base (the UMD core, an OBJECT library) and tt-umd (the workload
# layer and the library consumers link, which links tt-umd-base publicly within the build).
#  - Shadow headers and POSIX stand-ins come BEFORE upstream's api/ directories. compat/public is
#    needed by consumers because upstream public headers include <execinfo.h>, <cxxabi.h> and
#    <pthread.h>; compat/private (<unistd.h>, <sys/mman.h>) only by UMD's own sources and tests.
#  - Designated initializers in public headers need C++20 on MSVC; tt-logger's macros use
#    __VA_OPT__, which needs the conforming preprocessor. Both PUBLIC, as consumers compile those
#    headers too.
#  - Forced includes: consumers get only the header-free public prelude (pid_t,
#    __atomic_always_lock_free); UMD's own sources get the private one, which adds <windows.h>
#    (whose IN/OUT/ERROR/... macros must not reach consumer code) on top of it.
function(tt_win_configure_umd_library target)
    target_include_directories(
        ${target}
        BEFORE
        PUBLIC
            "$<BUILD_INTERFACE:${_win_root}/include>"
            "$<BUILD_INTERFACE:${_win_root}/compat/public>"
        PRIVATE
            "${_win_root}/compat/private"
    )
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_compile_options(${target} PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:/Zc:preprocessor>")
    target_compile_options(
        ${target}
        PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:/FI${_win_root}/private/tt_umd_windows_private_prelude.h>"
        INTERFACE
            "$<$<COMPILE_LANGUAGE:CXX>:/FI${_win_root}/include/tt_umd_windows_prelude.h>"
    )
endfunction()

####################################################################################################
# tt-umd-utils
####################################################################################################

tt_win_require_targets(tt-umd-utils)

# The robust mutex is POSIX shared memory plus robust pthread mutexes; the Windows one is a named
# kernel mutex. stacktrace_windows.cpp backs <execinfo.h>, which error_detail.hpp (tt-umd-utils'
# public error types) uses.
tt_win_remove_sources(tt-umd-utils src/robust_mutex.cpp)
target_sources(
    tt-umd-utils
    PRIVATE
        "${_win_root}/tt-umd-utils/src/robust_mutex_windows.cpp"
        "${_win_root}/tt-umd-utils/src/stacktrace_windows.cpp"
)
tt_win_remove_link_items(tt-umd-utils rt)
tt_win_configure_umd_library(tt-umd-utils)

####################################################################################################
# tt-umd-base (UMD core) and tt-umd (workload layer)
####################################################################################################

tt_win_require_targets(tt-umd-base tt-umd)

# Linux-only sources: hugepages/mmap, sysfs, /dev chardevs, hwloc, Unix domain sockets. The ones
# whose symbols are needed are replaced by Windows implementations; the simulation socket transport
# has no Windows counterpart and is only referenced from code that is compiled with
# TT_UMD_BUILD_SIMULATION (unsupported on Windows).
tt_win_remove_sources(
    tt-umd-base
    src/pcie/pci_device.cpp
    src/hugepage.cpp
    src/cpuset_lib.cpp
    src/chip_helpers/silicon_sysmem_manager.cpp
    src/warm_reset.cpp
    src/simulation/simulation_server_socket.cpp
    src/simulation/simulation_client.cpp
    src/simulation/simulation_server_transport.cpp
)
target_sources(
    tt-umd-base
    PRIVATE
        "${_win_root}/tt-umd/src/pcie/pci_device_windows.cpp"
        "${_win_root}/tt-umd/src/chip_helpers/silicon_sysmem_manager_windows.cpp"
        "${_win_root}/tt-umd/src/warm_reset_windows.cpp"
        "${_win_root}/compat/posix_compat_windows.cpp"
)
tt_win_remove_link_items(tt-umd-base hwloc rt)
tt_win_configure_umd_library(tt-umd-base)
tt_win_configure_umd_library(tt-umd)

# GCC attributes in two sources (noinline, tls_model) carry no meaning for the Windows build.
# Scoped to these files so that an attribute anywhere else is still a compile error.
get_target_property(_umd_source_dir tt-umd-base SOURCE_DIR)
foreach(_source IN ITEMS src/pcie/tlb_window.cpp src/pcie/silicon_tlb_window.cpp)
    if(NOT EXISTS "${_umd_source_dir}/${_source}")
        tt_win_fail("upstream source '${_umd_source_dir}/${_source}' no longer exists.")
    endif()
endforeach()
set_source_files_properties(
    "${_umd_source_dir}/src/pcie/tlb_window.cpp"
    TARGET_DIRECTORY tt-umd-base
    PROPERTIES
        COMPILE_OPTIONS
            "/FI${_win_root}/private/tt_umd_windows_gnu_attributes.h"
)
# The SIGBUS guard: see windows/private/tt_umd_windows_sigbus.h.
set_source_files_properties(
    "${_umd_source_dir}/src/pcie/silicon_tlb_window.cpp"
    TARGET_DIRECTORY tt-umd-base
    PROPERTIES
        COMPILE_OPTIONS
            "/FI${_win_root}/private/tt_umd_windows_gnu_attributes.h;/FI${_win_root}/private/tt_umd_windows_sigbus.h"
)

####################################################################################################
# Tests
####################################################################################################

if(TT_UMD_BUILD_TESTS)
    tt_win_require_targets(
        test_common
        api_tests
        baremetal_tests
        destructive_tests
        umd_misc_tests
        unified_tests
        test_utils_tests
        tt_umd_utils_tests
    )

    # googletest is added through CPM as "GTest", and FetchContent lowercases that to "gtest",
    # which shadows the googletest project's own gtest_SOURCE_DIR inside googlemock's scope. On a
    # fresh fetch, gmock's sources then miss the googletest include directories (MSVC builds gmock
    # from gtest-all.cc directly). Point gmock back at the real googletest tree.
    tt_win_require_targets(gtest gmock gmock_main)
    get_target_property(_googletest_dir gtest SOURCE_DIR)
    foreach(_target IN ITEMS gmock gmock_main)
        target_include_directories(${_target} PRIVATE "${_googletest_dir}/include" "${_googletest_dir}")
    endforeach()

    tt_win_remove_link_items(test_common pthread)
    target_include_directories(test_common BEFORE INTERFACE "${_win_root}/compat/private" "${_win_root}/tests/compat")
    target_compile_options(test_common INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:/FI${_win_root}/tests/test_prelude.h>")

    # tt-umd-utils' tests link the library directly rather than through test_common.
    tt_win_remove_link_items(tt_umd_utils_tests pthread)

    # Tests built on fork()/waitpid(), POSIX signals, /dev/shm, /proc, perf_event or Unix domain
    # sockets, or on the Linux-only sources removed from tt-umd-base above.
    tt_win_remove_sources(api_tests test_sysmem_manager.cpp test_warm_reset.cpp test_frame_pointer.cpp)
    tt_win_remove_sources(
        baremetal_tests
        test_long_jump.cpp
        test_notification_mechanism.cpp
        test_simulation_server_socket.cpp
        test_simulation_client.cpp
        test_simulation_server_transport.cpp
        test_eth_ipc.cpp
        test_pci_device_ctor_failure.cpp
    )
    tt_win_remove_sources(destructive_tests test_safe_io_warm_reset.cpp)
    tt_win_remove_sources(tt_umd_utils_tests test_robust_mutex.cpp)
    tt_win_remove_sources(unified_tests "${_umd_root}/tt-umd-workload/tests/unified/multiprocess.cpp")
    if(TARGET umd_microbenchmark)
        tt_win_remove_sources(umd_microbenchmark benchmarks/pcie_dma/test_pcie_dma.cpp benchmarks/iommu/test_iommu.cpp)
    endif()

    # test_utils_tests only tests MultiProcessPipe (fork/pipe). Keep the target, which the
    # umd_tests aggregate depends on, as an empty test binary.
    tt_win_remove_sources(test_utils_tests test_utils.cpp)
    target_sources(test_utils_tests PRIVATE "${_win_root}/tests/empty_test.cpp")
endif()

tt_win_strip_gnu_warning_flags()
