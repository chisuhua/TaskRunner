cmake_minimum_required(VERSION 3.10)

project(async_task)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ============================================================
# H-5: Build mode option (test-fixture default | umd-evolution experimental)
# ============================================================
set(TASKRUNNER_BUILD_MODE "test-fixture" CACHE STRING
    "Build mode: test-fixture (default) | umd-evolution (experimental)")
set_property(CACHE TASKRUNNER_BUILD_MODE
    PROPERTY STRINGS "test-fixture" "umd-evolution")

if(NOT (TASKRUNNER_BUILD_MODE STREQUAL "test-fixture" OR
        TASKRUNNER_BUILD_MODE STREQUAL "umd-evolution"))
    message(FATAL_ERROR
        "Invalid TASKRUNNER_BUILD_MODE: ${TASKRUNNER_BUILD_MODE}. "
        "Valid values: test-fixture (default) | umd-evolution")
endif()

if(TASKRUNNER_BUILD_MODE STREQUAL "test-fixture")
    message(STATUS "Building TaskRunner in TEST-FIXTURE mode (default)")
else()
    message(WARNING "Building TaskRunner in UMD-EVOLUTION mode (experimental, not for production use)")
endif()

option(BUILD_CLI "Build CLI mode (default: OFF for test mode)" OFF)

enable_testing()

add_subdirectory(doctest)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/doctest/doctest)

# ============================================================
# Symlink verification for UsrLinuxEmu interface
# ============================================================
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/UsrLinuxEmu/plugins/gpu_driver/shared/gpu_ioctl.h)
    message(FATAL_ERROR "Symlink broken: UsrLinuxEmu/plugins/gpu_driver/shared/ not found. "
                       "Please ensure TaskRunner/UsrLinuxEmu symlink points to UsrLinuxEmu/")
endif()

# ============================================================
# gpu_driver_client - System C wrapper (test-fixture scope)
# ============================================================
set(GPU_CLIENT_SOURCES
    src/test_fixture/gpu_driver_client.cpp
)

# ============================================================
# H-5: scoped source variables
# ============================================================
set(SCOPED_SHARED_SOURCES
    src/shared/sync_primitives.cpp
    src/shared/memory_manager.cpp
)

set(SCOPED_TEST_FIXTURE_SOURCES
    src/test_fixture/cuda_scheduler.cpp
    src/test_fixture/cuda_stub.cpp
)

# ============================================================
# UMD-EVOLUTION mode: build umd-evolution skeleton (experimental)
# ============================================================
if(TASKRUNNER_BUILD_MODE STREQUAL "umd-evolution")
    add_executable(test_umd_skeleton
        tests/umd/test_umd_skeleton.cpp
        src/umd/cuda_api.cpp
        src/umd/module_loader.cpp
        src/umd/ring_buffer.cpp
    )
    target_link_libraries(test_umd_skeleton doctest::doctest)
    set_target_properties(test_umd_skeleton PROPERTIES OUTPUT_NAME "test_umd_skeleton")
    add_test(NAME test_umd_skeleton COMMAND test_umd_skeleton)

    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    return()
endif()

# ============================================================
# TEST-FIXTURE mode (default): build test executables + V1/CLI binary
# ============================================================

add_executable(test_cuda_scheduler
    tests/test_fixture/test_cuda_scheduler.cpp
    ${SCOPED_TEST_FIXTURE_SOURCES}
    ${SCOPED_SHARED_SOURCES}
    ${GPU_CLIENT_SOURCES}
)
target_link_libraries(test_cuda_scheduler doctest::doctest)
set_target_properties(test_cuda_scheduler PROPERTIES OUTPUT_NAME "test_cuda_scheduler")

# H-2.5: GPU architecture foundation tests (IGpuDriver + DI + Mock)
add_executable(test_gpu_architecture
    tests/test_fixture/test_gpu_architecture.cpp
    ${SCOPED_TEST_FIXTURE_SOURCES}
    ${SCOPED_SHARED_SOURCES}
    ${GPU_CLIENT_SOURCES}
)
target_link_libraries(test_gpu_architecture doctest::doctest)
set_target_properties(test_gpu_architecture PROPERTIES OUTPUT_NAME "test_gpu_architecture")
add_test(NAME test_gpu_architecture COMMAND test_gpu_architecture)

# H-3: Phase 2 methods integration test (5 success + 4 mock-behavior + 1 R2 mapping + 2 R2 bonus)
add_executable(test_gpu_phase2
    tests/test_fixture/test_gpu_phase2.cpp
    ${SCOPED_TEST_FIXTURE_SOURCES}
    ${SCOPED_SHARED_SOURCES}
    ${GPU_CLIENT_SOURCES}
)
target_link_libraries(test_gpu_phase2 doctest::doctest)
set_target_properties(test_gpu_phase2 PROPERTIES OUTPUT_NAME "test_gpu_phase2")
add_test(NAME test_gpu_phase2 COMMAND test_gpu_phase2)

if(BUILD_CLI)
    add_executable(taskrunner
        src/test_fixture/cli_main.cpp
        src/test_fixture/cmd_buffer_v2.cpp
        src/test_fixture/cmd_cuda.cpp
        ${SCOPED_TEST_FIXTURE_SOURCES}
        ${SCOPED_SHARED_SOURCES}
        ${GPU_CLIENT_SOURCES}
    )
    set_target_properties(taskrunner PROPERTIES OUTPUT_NAME "taskrunner")
else()
    add_executable(async_task
        sample/main.cpp
        src/test_fixture/CmdProcessor.cpp
        src/test_fixture/TaskRunner.cpp
    )
    target_link_libraries(async_task doctest::doctest)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)