cmake_minimum_required(VERSION 3.20)
project(TaskRunner CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build mode option
set(TASKRUNNER_BUILD_MODE "test-fixture" CACHE STRING
    "Build mode: test-fixture (default) | umd-evolution")
set_property(CACHE TASKRUNNER_BUILD_MODE
    PROPERTY STRINGS "test-fixture" "umd-evolution")

# Doctest: prefer system -> vendored copy -> FetchContent (in that order)
find_package(doctest QUIET)
if(NOT doctest_FOUND)
    if(EXISTS ${CMAKE_SOURCE_DIR}/doctest/CMakeLists.txt)
        message(STATUS "TaskRunner: using vendored doctest copy at ${CMAKE_SOURCE_DIR}/doctest")
        add_subdirectory(doctest)
    else()
        message(STATUS "TaskRunner: fetching doctest from GitHub")
        include(FetchContent)
        FetchContent_Declare(doctest
            GIT_REPOSITORY https://github.com/doctest/doctest.git
            GIT_TAG v2.4.12
        )
        FetchContent_MakeAvailable(doctest)
    endif()
endif()

# Shared infrastructure (always built)
include(${CMAKE_SOURCE_DIR}/cmake/Shared.cmake)

# Conditional scope builds
if(TASKRUNNER_BUILD_MODE STREQUAL "test-fixture")
    message(STATUS "Building TaskRunner in TEST-FIXTURE mode (default)")
    include(${CMAKE_SOURCE_DIR}/cmake/TestFixture.cmake)
elseif(TASKRUNNER_BUILD_MODE STREQUAL "umd-evolution")
    message(STATUS "Building TaskRunner in UMD-EVOLUTION mode (experimental, not for production use)")
    include(${CMAKE_SOURCE_DIR}/cmake/UMDEvolution.cmake)
else()
    message(FATAL_ERROR "Unknown TASKRUNNER_BUILD_MODE: ${TASKRUNNER_BUILD_MODE}")
endif()
