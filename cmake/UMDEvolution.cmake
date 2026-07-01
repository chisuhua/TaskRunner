# cmake/UMDEvolution.cmake
# SCOPE: umd-evolution
# umd-evolution scope：UMD stub skeleton (experimental, not for production)
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

# Tests
add_executable(test_cuda_runtime_api
    tests/umd/test_cuda_runtime_api.cpp
)
target_link_libraries(test_cuda_runtime_api PRIVATE taskrunner_umd_stub)
add_test(NAME test_cuda_runtime_api COMMAND test_cuda_runtime_api)
