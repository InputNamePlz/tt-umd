set(MICROBENCHMARK_SRC_FILES
    benchmarks/tlb/test_tlb.cpp
    benchmarks/open_cluster/test_open_cluster.cpp
    benchmarks/ethernet_io/test_ethernet_io.cpp
    benchmarks/simulator/test_simulator.cpp
)
if(NOT WIN32)
    # Linux-only: both benchmarks map DMA buffers directly via mmap()/sys/mman.h (test_iommu.cpp
    # also uses sysconf()/unistd.h), which have no Windows equivalent in this codebase yet.
    list(
        APPEND
        MICROBENCHMARK_SRC_FILES
        benchmarks/pcie_dma/test_pcie_dma.cpp
        benchmarks/iommu/test_iommu.cpp
    )
endif()
add_executable(umd_microbenchmark ${MICROBENCHMARK_SRC_FILES})
target_link_libraries(
    umd_microbenchmark
    PRIVATE
        test_common
        nanobench
)
target_include_directories(
    umd_microbenchmark
    PRIVATE
        ${nanobench_SOURCE_DIR}/src/include
        .
)
set_target_properties(
    umd_microbenchmark
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/microbenchmark
)

# Host-side one-off benchmarks (e.g. locking backends) live in their own subdirectory with a
# dedicated CMakeLists, separate from the regular device microbenchmarks above.
add_subdirectory(host_benchmark)
