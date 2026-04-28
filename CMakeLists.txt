cmake_minimum_required(VERSION 3.10)

project(async_task)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Build option: CLI mode vs Test mode
option(BUILD_CLI "Build CLI mode (default: OFF for test mode)" OFF)

# Add doctest as a subdirectory
add_subdirectory(doctest)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/doctest/doctest)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../UsrLinuxEmu/include)

# 测试可执行文件（始终构建）
add_executable(test_cuda_scheduler
    tests/test_cuda_scheduler.cpp
    src/cuda_scheduler.cpp
    src/sync_primitives.cpp
    src/memory_manager.cpp
    src/cuda_stub.cpp
)
target_link_libraries(test_cuda_scheduler doctest::doctest)
set_target_properties(test_cuda_scheduler PROPERTIES OUTPUT_NAME "test_cuda_scheduler")

if(BUILD_CLI)
    # CLI 模式：构建命令行工具
    add_executable(taskrunner
        src/cli_main.cpp
        src/cmd_buffer_v2.cpp
        src/cmd_cuda.cpp
        # CUDA Scheduler 模块 (DDS v1.2)
        src/cuda_scheduler.cpp
        src/sync_primitives.cpp
        src/memory_manager.cpp
        src/cuda_stub.cpp
    )
    
    # CLI 模式不需要 doctest
    # target_link_libraries(taskrunner)
    
    # 设置输出名称
    set_target_properties(taskrunner PROPERTIES OUTPUT_NAME "taskrunner")
else()
    # 测试模式：构建原有测试程序
    add_executable(async_task 
        sample/main.cpp
        src/CmdProcessor.cpp
        src/TaskRunner.cpp
    )
    
    # Link doctest library
    target_link_libraries(async_task doctest::doctest)
endif()

