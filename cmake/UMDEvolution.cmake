# cmake/UMDEvolution.cmake
# SCOPE: umd-evolution
# UMD shim skeleton — built by default (since 2026-07-09; see
# openspec/changes/umd-evolution-build-default-on which supersedes tadr-108).
# "experimental" refers to FEATURE COMPLETENESS (see tadr-205 roadmap), NOT
# to build-mode coupling. For pure CLI / test-fixture-only builds, use
# TASKRUNNER_BUILD_MODE=test-fixture.
#
# Phase 1: depends on the test-fixture CudaScheduler (wrapped by CudaRuntimeApi).
# Future phases will remove this dependency as the UMD gains self-contained dispatch.

# CudaRuntimeApi wraps CudaScheduler; build the test-fixture library too.
include(${CMAKE_SOURCE_DIR}/cmake/TestFixture.cmake)

add_library(taskrunner_umd_stub SHARED
    src/umd/cuda_runtime_api.cpp
)
target_include_directories(taskrunner_umd_stub PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(taskrunner_umd_stub PUBLIC taskrunner_shared taskrunner_test_fixture)
target_compile_features(taskrunner_umd_stub PUBLIC cxx_std_17)
set_target_properties(taskrunner_umd_stub PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Phase 2: LD_PRELOAD shim — libcuda_taskrunner.so
add_library(cuda_taskrunner SHARED
    src/umd/libcuda_shim/cu_init.cpp
    src/umd/libcuda_shim/cu_module.cpp
    src/umd/libcuda_shim/cu_mem.cpp
    src/umd/libcuda_shim/cu_launch.cpp
    src/umd/libcuda_shim/cu_ctx.cpp
    src/umd/libcuda_shim/cu_device.cpp
    src/umd/libcuda_shim/cu_query.cpp
    src/umd/libcuda_shim/cu_stream.cpp
    src/umd/libcuda_shim/cu_stream_capture.cpp
    src/umd/libcuda_shim/cu_graph.cpp
    src/umd/libcuda_shim/cu_graph_node.cpp
    src/umd/libcuda_shim/cu_graph_exec.cpp
    src/umd/libcuda_shim/cu_mem_pool.cpp
    src/umd/libcuda_shim/cu_event.cpp
    src/umd/libcuda_shim/cu_array.cpp
    src/umd/libcuda_shim/cu_texref.cpp
)
target_include_directories(cuda_taskrunner PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(cuda_taskrunner PUBLIC
    taskrunner_test_fixture
    taskrunner_shared
    taskrunner_umd_stub
    dl
    pthread
)
target_compile_features(cuda_taskrunner PUBLIC cxx_std_17)
set_target_properties(cuda_taskrunner PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Tests
add_executable(test_cuda_runtime_api
    tests/umd/test_cuda_runtime_api.cpp
)
target_link_libraries(test_cuda_runtime_api PRIVATE taskrunner_umd_stub)
add_test(NAME test_cuda_runtime_api COMMAND test_cuda_runtime_api)

# Phase 2: Shim E2E test (links against libcuda_taskrunner.so directly).
add_executable(test_cuda_shim
    tests/umd/test_cuda_shim.cpp
)
target_link_libraries(test_cuda_shim PRIVATE cuda_taskrunner)
add_test(NAME test_cuda_shim COMMAND test_cuda_shim)

# Phase 3.1+3.2: per-API shim E2E tests (links against libcuda_taskrunner.so).
add_executable(test_cu_stream_capture
    tests/umd/test_cu_stream_capture.cpp
)
target_link_libraries(test_cu_stream_capture PRIVATE cuda_taskrunner)
add_test(NAME test_cu_stream_capture COMMAND test_cu_stream_capture)

add_executable(test_cu_graph
    tests/umd/test_cu_graph.cpp
)
target_link_libraries(test_cu_graph PRIVATE cuda_taskrunner)
add_test(NAME test_cu_graph COMMAND test_cu_graph)

add_executable(test_cu_mem_pool
    tests/umd/test_cu_mem_pool.cpp
)
target_link_libraries(test_cu_mem_pool PRIVATE cuda_taskrunner)
add_test(NAME test_cu_mem_pool COMMAND test_cu_mem_pool)

# Phase 3.3a: Event timing precision
add_executable(test_event_timing
    tests/umd/test_event_timing.cpp
)
target_link_libraries(test_event_timing PRIVATE cuda_taskrunner)
add_test(NAME test_event_timing COMMAND test_event_timing)

# Phase 3.3b: Texture/Surface frontend
add_executable(test_texture_surface
    tests/umd/test_texture_surface.cpp
)
target_link_libraries(test_texture_surface PRIVATE cuda_taskrunner)
add_test(NAME test_texture_surface COMMAND test_texture_surface)
