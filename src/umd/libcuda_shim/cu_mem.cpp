// SCOPE: UMD-EVOLUTION
// cu_mem.cpp - Memory allocation and copy API implementations.
//
// Routes cuMemAlloc/cuMemcpyHtoD/cuMemcpyDtoH to Phase 1's CudaRuntimeApi.
// cuMemcpyDtoD returns NOT_SUPPORTED (Phase 1 limitation).
// cuMemFree is a no-op (Phase 1 malloc has no free).
//
// See design doc: docs/superpowers/specs/2026-06-30-umd-evolution-redesign.md

#include <cuda.h>
#include "umd/cuda_runtime_api.hpp"

namespace async_task::umd::shim {
extern CudaRuntimeApi* runtime();
}  // namespace async_task::umd::shim

using async_task::umd::CudaError;
using async_task::umd::CudaMemcpyKind;

extern "C" CUresult cuMemAlloc(CUdeviceptr* dptr, size_t bytesize) {
  if (!dptr) return CUDA_ERROR_INVALID_VALUE;
  if (bytesize == 0) return CUDA_ERROR_INVALID_VALUE;

  void* ptr = nullptr;
  auto err = async_task::umd::shim::runtime()->malloc(&ptr, bytesize);
  if (err != CudaError::Success) {
    if (err == CudaError::OutOfMemory) return CUDA_ERROR_OUT_OF_MEMORY;
    return CUDA_ERROR_UNKNOWN;
  }
  *dptr = reinterpret_cast<CUdeviceptr>(ptr);
  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemFree(CUdeviceptr dptr) {
  // Phase 1 limitation: CudaRuntimeApi malloc has no free.
  // No-op to maintain API stability.
  (void)dptr;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemcpyHtoD(CUdeviceptr dstDevice,
                                  const void* srcHost,
                                  size_t ByteCount) {
  if (!dstDevice || !srcHost) return CUDA_ERROR_INVALID_VALUE;
  auto err = async_task::umd::shim::runtime()->memcpy(
      reinterpret_cast<void*>(dstDevice), srcHost, ByteCount,
      CudaMemcpyKind::HostToDevice);
  return err == CudaError::Success ? CUDA_SUCCESS : CUDA_ERROR_UNKNOWN;
}

extern "C" CUresult cuMemcpyDtoH(void* dstHost,
                                  CUdeviceptr srcDevice,
                                  size_t ByteCount) {
  if (!dstHost || !srcDevice) return CUDA_ERROR_INVALID_VALUE;
  auto err = async_task::umd::shim::runtime()->memcpy(
      dstHost, reinterpret_cast<void*>(srcDevice), ByteCount,
      CudaMemcpyKind::DeviceToHost);
  return err == CudaError::Success ? CUDA_SUCCESS : CUDA_ERROR_UNKNOWN;
}

extern "C" CUresult cuMemcpyDtoD(CUdeviceptr dstDevice,
                                  CUdeviceptr srcDevice,
                                  size_t ByteCount) {
  // Phase 1 limitation: Phase 1 CudaRuntimeApi::memcpy returns
  // CudaError::NotSupported for D2D. Shim surfaces as
  // CUDA_ERROR_NOT_SUPPORTED.
  (void)dstDevice; (void)srcDevice; (void)ByteCount;
  return CUDA_ERROR_NOT_SUPPORTED;
}

extern "C" CUresult cuMemcpy(CUdeviceptr dst, CUdeviceptr src,
                              size_t ByteCount) {
  // cuMemcpy is generic; Phase 2 limitation: real CUDA Driver API
  // distinguishes via cuPointerGetAttribute, but that's not implemented yet.
  // Conservative: return NOT_SUPPORTED.
  (void)dst; (void)src; (void)ByteCount;
  return CUDA_ERROR_NOT_SUPPORTED;
}

extern "C" CUresult cuMemcpyAsync(CUdeviceptr dst, CUdeviceptr src,
                                   size_t ByteCount, CUstream hStream) {
  (void)dst; (void)src; (void)ByteCount; (void)hStream;
  return CUDA_ERROR_NOT_IMPLEMENTED;
}

extern "C" CUresult cuMemsetD32(CUdeviceptr dstDevice, unsigned int ui,
                                 size_t N) {
  (void)dstDevice; (void)ui; (void)N;
  return CUDA_ERROR_NOT_IMPLEMENTED;
}

extern "C" CUresult cuMemsetD8(CUdeviceptr dstDevice, unsigned char uc,
                                size_t N) {
  (void)dstDevice; (void)uc; (void)N;
  return CUDA_ERROR_NOT_IMPLEMENTED;
}

extern "C" CUresult cuMemAllocHost(void** pp, size_t bytesize,
                                    unsigned int Flags) {
  (void)pp; (void)bytesize; (void)Flags;
  return CUDA_ERROR_NOT_IMPLEMENTED;
}

extern "C" CUresult cuMemFreeHost(void* p) {
  (void)p;
  return CUDA_ERROR_NOT_IMPLEMENTED;
}

extern "C" CUresult cuMemAllocManaged(CUdeviceptr* dptr, size_t bytesize,
                                     unsigned int flags) {
  (void)dptr; (void)bytesize; (void)flags;
  return CUDA_ERROR_NOT_IMPLEMENTED;
}

extern "C" CUresult cuMemAllocPitch(CUdeviceptr* dptr, size_t* pitch,
                                     size_t width, size_t height,
                                     unsigned int elementSizeBytes) {
  (void)dptr; (void)pitch; (void)width; (void)height; (void)elementSizeBytes;
  return CUDA_ERROR_NOT_IMPLEMENTED;
}

extern "C" CUresult cuMemGetInfo(size_t* free, size_t* total) {
  if (!free || !total) return CUDA_ERROR_INVALID_VALUE;
  *free = 4ULL * 1024 * 1024 * 1024;     // 4 GB free
  *total = 8ULL * 1024 * 1024 * 1024;    // 8 GB total
  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemGetAddressRange(CUdeviceptr* base, size_t* size,
                                          CUdeviceptr dptr) {
  (void)base; (void)size; (void)dptr;
  return CUDA_ERROR_NOT_IMPLEMENTED;
}
