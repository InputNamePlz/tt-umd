set(DESTRUCTIVE_TESTS_SRCS
    test_hang_detection.cpp
    test_warm_reset_after_noc_hang.cpp
)

if(NOT WIN32)
    # Linux-only: forks child processes (fork/waitpid) and installs a SIGBUS handler to test the
    # safe I/O path across a warm reset -- neither exists on Windows.
    list(APPEND DESTRUCTIVE_TESTS_SRCS test_safe_io_warm_reset.cpp)
endif()

add_executable(destructive_tests ${DESTRUCTIVE_TESTS_SRCS})
target_link_libraries(destructive_tests PRIVATE test_common)
set_target_properties(
    destructive_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/destructive
        OUTPUT_NAME
            destructive_tests
)
