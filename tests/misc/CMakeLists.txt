set(UMD_MISC_TESTS_SRCS
    test_semver.cpp
    test_errors.cpp
    test_kmd_mutex.cpp
    test_op_timeout_guard.cpp
)

if(NOT WIN32)
    # Linux-only: exercises the pthread robust-mutex implementation via /dev/shm, fork and waitpid;
    # Windows builds robust_mutex_windows.cpp with named-mutex semantics instead.
    list(APPEND UMD_MISC_TESTS_SRCS test_robust_mutex.cpp)
endif()

add_executable(umd_misc_tests ${UMD_MISC_TESTS_SRCS})
target_link_libraries(umd_misc_tests PRIVATE test_common)
set_target_properties(
    umd_misc_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/misc
        OUTPUT_NAME
            umd_misc_tests
)
