cmake_minimum_required(VERSION 3.20)
project(TaskRunner CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Sanitizer options (mutually exclusive: ASan vs TSan).
# CI workflow consumes these via -DSANITIZER_ADDRESS=ON etc.
# ASan + UBSan can be combined; TSan must be standalone.
option(SANITIZER_ADDRESS "Enable AddressSanitizer (ASan)" OFF)
option(SANITIZER_UNDEFINED "Enable UndefinedBehaviorSanitizer (UBSan)" OFF)
option(SANITIZER_THREAD "Enable ThreadSanitizer (TSan)" OFF)

if(SANITIZER_ADDRESS AND SANITIZER_THREAD)
    message(FATAL_ERROR
        "ASan (SANITIZER_ADDRESS) and TSan (SANITIZER_THREAD) cannot be "
        "enabled simultaneously (LLVM limitation). Use them in separate "
        "builds.")
endif()

if(SANITIZER_ADDRESS OR SANITIZER_UNDEFINED OR SANITIZER_THREAD)
    message(STATUS "TaskRunner: sanitizer mode active "
        "(ASan=${SANITIZER_ADDRESS} UBSan=${SANITIZER_UNDEFINED} TSan=${SANITIZER_THREAD})")
endif()

if(SANITIZER_ADDRESS)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
endif()

if(SANITIZER_UNDEFINED)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=undefined)
endif()

if(SANITIZER_THREAD)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
endif()

# Build mode option
# Default changed from "test-fixture" to "umd-evolution" on 2026-07-09
# (see openspec/changes/umd-evolution-build-default-on, supersedes tadr-108).
# "test-fixture" remains as opt-out for users who want only the CLI + tests/test_fixture
# without the LD_PRELOAD shim (libcuda_taskrunner.so) or tests/umd/.
set(TASKRUNNER_BUILD_MODE "umd-evolution" CACHE STRING
    "Build mode: umd-evolution (default, includes libcuda_shim + tests/umd) | test-fixture (opt-out)")
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
# Default = umd-evolution (full UMD build). test-fixture is an explicit opt-out.
# Note: UMDEvolution.cmake internally includes TestFixture.cmake — do NOT re-include.
if(TASKRUNNER_BUILD_MODE STREQUAL "test-fixture")
    message(STATUS "Building TaskRunner in TEST-FIXTURE mode (opt-out; excludes libcuda_shim + tests/umd)")
    include(${CMAKE_SOURCE_DIR}/cmake/TestFixture.cmake)
elseif(TASKRUNNER_BUILD_MODE STREQUAL "umd-evolution")
    message(STATUS "Building TaskRunner in UMD-EVOLUTION mode (default; includes libcuda_shim + tests/umd)")
    include(${CMAKE_SOURCE_DIR}/cmake/UMDEvolution.cmake)
else()
    message(FATAL_ERROR "Unknown TASKRUNNER_BUILD_MODE: ${TASKRUNNER_BUILD_MODE}")
endif()
