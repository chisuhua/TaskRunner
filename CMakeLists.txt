cmake_minimum_required(VERSION 3.10)
project(TaskRunner)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add doctest as a submodule or download it
if(EXISTS "${PROJECT_SOURCE_DIR}/../doctest/doctest/doctest.h")
    set(DOCTEST_ROOT ${PROJECT_SOURCE_DIR}/../doctest)
elseif(EXISTS "${PROJECT_SOURCE_DIR}/external/doctest/doctest/doctest.h")
    set(DOCTEST_ROOT ${PROJECT_SOURCE_DIR}/external/doctest)
elseif()
    message(STATUS "Downloading doctest...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_SOURCE_DIR}/external/doctest
    )
    execute_process(
        COMMAND git clone https://github.com/doctest/doctest.git ${PROJECT_SOURCE_DIR}/external/doctest/doctest --branch master --single-branch
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/external/doctest
    )
    set(DOCTEST_ROOT ${PROJECT_SOURCE_DIR}/external/doctest)
endif()

#find_package(doctest REQUIRED PATHS ${DOCTEST_ROOT}/scripts/cmake)

include_directories(${DOCTEST_ROOT}/doctest)
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/include/taskrunner)

add_library(taskrunner SHARED src/barrier.cpp src/taskqueue.cpp src/worker.cpp src/scheduler.cpp)
target_compile_definitions(taskrunner PUBLIC -D_EXPORT)
#target_link_libraries(taskrunner doctest::doctest)

add_executable(${PROJECT_NAME} src/main.cpp)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_link_libraries(${PROJECT_NAME} taskrunner)


enable_testing()
add_executable(test_taskrunner tests/test_taskrunner.cpp)
#target_link_libraries(test_taskrunner doctest::doctest)
target_link_libraries(test_taskrunner taskrunner)
add_test(NAME TestTaskRunner COMMAND test_taskrunner)
