cmake_minimum_required(VERSION 3.10)

project(async_task)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

option(BUILD_CLI "Build CLI mode (default: OFF for test mode)" OFF)

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
# gpu_driver_client - System C wrapper
# ============================================================
set(GPU_CLIENT_SOURCES
    src/gpu_driver_client.cpp
)

# ============================================================
# Test executable (always built)
# ============================================================
add_executable(test_cuda_scheduler
    tests/test_cuda_scheduler.cpp
    src/cuda_scheduler.cpp
    src/sync_primitives.cpp
    src/memory_manager.cpp
    src/cuda_stub.cpp
    ${GPU_CLIENT_SOURCES}
)
target_link_libraries(test_cuda_scheduler doctest::doctest)
set_target_properties(test_cuda_scheduler PROPERTIES OUTPUT_NAME "test_cuda_scheduler")

if(BUILD_CLI)
    add_executable(taskrunner
        src/cli_main.cpp
        src/cmd_buffer_v2.cpp
        src/cmd_cuda.cpp
        src/cuda_scheduler.cpp
        src/sync_primitives.cpp
        src/memory_manager.cpp
        src/cuda_stub.cpp
        ${GPU_CLIENT_SOURCES}
    )
    set_target_properties(taskrunner PROPERTIES OUTPUT_NAME "taskrunner")
else()
    add_executable(async_task
        sample/main.cpp
        src/CmdProcessor.cpp
        src/TaskRunner.cpp
    )
    target_link_libraries(async_task doctest::doctest)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
