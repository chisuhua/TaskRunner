// SCOPE: UMD-EVOLUTION
// STATUS: PROPOSED
// IMPLEMENTED: NO

#include "umd/cuda_api.hpp"

namespace async_task::umd {

void* CudaApi::alloc(std::size_t size) {
    (void)size;
    return nullptr;  // PoC not implemented
}

int CudaApi::memcpy(void* dst, const void* src, std::size_t size) {
    (void)dst; (void)src; (void)size;
    return -38;  // -ENOSYS
}

int CudaApi::launch_kernel(const char* kernel_name, void** args, int num_args) {
    (void)kernel_name; (void)args; (void)num_args;
    return -38;  // -ENOSYS
}

}  // namespace async_task::umd
