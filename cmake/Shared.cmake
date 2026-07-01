# cmake/Shared.cmake
# SCOPE: shared
# 共享基础设施：跨 scope 复用
# (test-fixture + umd-evolution 都依赖 taskrunner_shared)
add_library(taskrunner_shared STATIC
    src/shared/memory_manager.cpp
    src/shared/sync_primitives.cpp
)
target_include_directories(taskrunner_shared PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(taskrunner_shared PUBLIC doctest::doctest)
target_compile_features(taskrunner_shared PUBLIC cxx_std_17)
set_target_properties(taskrunner_shared PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
