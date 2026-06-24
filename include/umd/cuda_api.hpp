// SCOPE: UMD-EVOLUTION
// STATUS: PROPOSED
// IMPLEMENTED: NO

#pragma once

#include <cstddef>

namespace async_task::umd {

// Minimal CUDA Runtime API surface (PoC only — NOT IMPLEMENTED)
class CudaApi {
public:
    // Allocate device memory. Returns nullptr (PoC not implemented).
    void* alloc(std::size_t size);

    // Copy memory. Returns -ENOSYS (38) (PoC not implemented).
    int memcpy(void* dst, const void* src, std::size_t size);

    // Launch kernel by name. Returns -ENOSYS (PoC not implemented).
    int launch_kernel(const char* kernel_name, void** args, int num_args);
};

}  // namespace async_task::umd
