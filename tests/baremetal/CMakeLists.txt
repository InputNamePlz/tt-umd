set(BAREMETAL_TESTS_SRCS
    test_architecture_tlbs.cpp
    test_cluster_descriptor_offline.cpp
    test_core_coord_translation_wh.cpp
    test_core_coord_translation_bh.cpp
    test_core_coord_translation_quasar.cpp
    test_soc_arch_descriptor.cpp
    test_soc_descriptor.cpp
    test_cluster.cpp
    test_blackhole_arc_apb.cpp
    test_wormhole_arc_window.cpp
    test_blackhole_arc_message_queue.cpp
)

if(NOT WIN32)
    # Linux-only tests: both fork child processes and reap them with waitpid.
    list(
        APPEND
        BAREMETAL_TESTS_SRCS
        test_long_jump.cpp
        test_notification_mechanism.cpp
    )
    # SimulationServerSocket and SimulationClient have no simulation deps (always compiled), so
    # their tests need no card and no simulation build -- they run in the no-card baremetal CI lane.
    # Linux-only: the transport is a Unix domain socket, and the sources under test are excluded
    # from the Windows build of tt-umd.
    list(
        APPEND
        BAREMETAL_TESTS_SRCS
        test_simulation_server_socket.cpp
        test_simulation_client.cpp
        test_simulation_server_transport.cpp
    )
endif()

add_executable(baremetal_tests ${BAREMETAL_TESTS_SRCS})
target_link_libraries(baremetal_tests PRIVATE test_common)
if(TT_UMD_BUILD_EMULE)
    target_sources(baremetal_tests PRIVATE test_sw_emule_chip.cpp)
endif()
if(TT_UMD_BUILD_SIMULATION)
    target_sources(baremetal_tests PRIVATE test_simulation_tlb_allocator.cpp)
endif()
set_target_properties(
    baremetal_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/baremetal
        OUTPUT_NAME
            baremetal_tests
)
