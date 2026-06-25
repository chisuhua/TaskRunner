# cmake/UMDEvolution.cmake
# SCOPE: umd-evolution
# umd-evolution scope：UMD stub skeleton (experimental, not for production)
add_library(taskrunner_umd_stub SHARED
    src/umd/cuda_api.cpp
    src/umd/module_loader.cpp
    src/umd/ring_buffer.cpp
)
target_include_directories(taskrunner_umd_stub PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(taskrunner_umd_stub PUBLIC taskrunner_shared)
target_compile_features(taskrunner_umd_stub PUBLIC cxx_std_17)
set_target_properties(taskrunner_umd_stub PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Tests
add_executable(test_umd_skeleton
    tests/umd/test_umd_skeleton.cpp
)
target_link_libraries(test_umd_skeleton PRIVATE taskrunner_umd_stub)
add_test(NAME test_umd_skeleton COMMAND test_umd_skeleton)
