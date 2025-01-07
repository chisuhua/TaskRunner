cmake_minimum_required(VERSION 3.10)

project(async_task_system)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add doctest as a subdirectory
add_subdirectory(doctest)

# Add the main executable
add_executable(async_task_system main.cpp)

# Include directories
target_include_directories(async_task_system PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Link doctest library
target_link_libraries(async_task_system doctest::doctest)

