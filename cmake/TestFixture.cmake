# cmake/TestFixture.cmake
# SCOPE: test-fixture
# test-fixture scope：CUDA stub + scheduler + GPU driver client + tests + CLI
add_library(taskrunner_test_fixture STATIC
    src/test_fixture/cuda_stub.cpp
    src/test_fixture/cuda_scheduler.cpp
    src/test_fixture/gpu_driver_client.cpp
)
target_include_directories(taskrunner_test_fixture PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(taskrunner_test_fixture PUBLIC taskrunner_shared)
target_compile_features(taskrunner_test_fixture PUBLIC cxx_std_17)

# CLI
add_executable(taskrunner src/test_fixture/cmd_cuda.cpp)
target_link_libraries(taskrunner PRIVATE taskrunner_test_fixture)

# Tests
enable_testing()

add_executable(test_cuda_scheduler
    tests/test_fixture/test_cuda_scheduler.cpp
    tests/test_fixture/test_gpu_architecture.cpp
    tests/test_fixture/test_gpu_phase2.cpp
    tests/test_fixture/mock_gpu_driver.hpp
)
target_link_libraries(test_cuda_scheduler PRIVATE taskrunner_test_fixture)
add_test(NAME test_cuda_scheduler COMMAND test_cuda_scheduler)
