// SCOPE: SHARED
// include/error_handling.hpp - H-5 Phase B placeholder
// Scope: shared (minimal implementation to satisfy spec-shared-infrastructure L49-55)
#pragma once
#include <cstdint>

namespace async_task::gpu {

enum class ErrorCode : int32_t {
    SUCCESS       = 0,
    FAILURE       = -1,
    NOT_IMPL      = -2,
    INVALID_ARG   = -3,
    NO_MEMORY     = -4,
};

template <typename T>
struct Result {
    ErrorCode code;
    T value;
    Result(ErrorCode c, T v) : code(c), value(std::move(v)) {}
    bool ok() const { return code == ErrorCode::SUCCESS; }
};

}  // namespace async_task::gpu
