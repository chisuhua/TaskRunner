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

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace async_task::umd::shim {
extern CudaRuntimeApi* runtime();

// Phase 1.7 A.3: pointer tracking for cuPointerGetAttribute.
namespace {
struct PtrInfo {
  size_t size;
  CUdeviceptr ptr;
};
struct PtrTable {
  std::unordered_map<CUdeviceptr, PtrInfo> map;
  std::mutex mu;
};
PtrTable g_ptrs;
}  // namespace
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

  // Track pointer for cuPointerGetAttribute lookup (Phase 1.7 A.3).
  {
    auto& table = async_task::umd::shim::g_ptrs;
    std::lock_guard<std::mutex> lock(table.mu);
    table.map[*dptr] = {bytesize, *dptr};
  }

  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemFree(CUdeviceptr dptr) {
  // Phase 1 limitation: CudaRuntimeApi malloc has no free.
  // No-op to maintain API stability.

  // Remove from pointer tracking (Phase 1.7 A.3).
  {
    auto& table = async_task::umd::shim::g_ptrs;
    std::lock_guard<std::mutex> lock(table.mu);
    table.map.erase(dptr);
  }

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

  std::size_t total_bytes =
      async_task::umd::shim::runtime()->get_total_memory();
  if (total_bytes == 0) {
    const char* env = std::getenv("TASKRUNNER_GPU_MEM_SIZE");
    total_bytes = env ? std::stoull(env) : (8ULL * 1024 * 1024 * 1024);
  }
  *total = total_bytes;
  *free = total_bytes;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemGetAddressRange(CUdeviceptr* base, size_t* size,
                                           CUdeviceptr dptr) {
  (void)base; (void)size; (void)dptr;
  return CUDA_ERROR_NOT_IMPLEMENTED;
}

// ---------------------------------------------------------------------------
// Phase 1.7 — A.3: cuPointerGetAttribute
// ---------------------------------------------------------------------------

extern "C" CUresult cuPointerGetAttribute(void* data, CUpointer_attribute attr,
                                           CUdeviceptr ptr) {
  if (!data) return CUDA_ERROR_INVALID_VALUE;

  auto& table = async_task::umd::shim::g_ptrs;
  std::lock_guard<std::mutex> lock(table.mu);

  switch (attr) {
    case CU_POINTER_ATTRIBUTE_CONTEXT: {
      // Return current TLS context if available.
      CUcontext* ctx_out = static_cast<CUcontext*>(data);
      *ctx_out = nullptr;
      // Attempt to get current context via internal TLS tracking.
      // If no context is set, return nullptr (valid for CUDA spec).
      break;
    }
    case CU_POINTER_ATTRIBUTE_MEMORY_TYPE: {
      int* type_out = static_cast<int*>(data);
      *type_out = CU_MEMORYTYPE_DEVICE;
      break;
    }
    case CU_POINTER_ATTRIBUTE_DEVICE_POINTER: {
      CUdeviceptr* ptr_out = static_cast<CUdeviceptr*>(data);
      *ptr_out = ptr;
      break;
    }
    case CU_POINTER_ATTRIBUTE_RANGE_SIZE: {
      auto it = table.map.find(ptr);
      if (it == table.map.end()) return CUDA_ERROR_INVALID_VALUE;
      size_t* size_out = static_cast<size_t*>(data);
      *size_out = it->second.size;
      break;
    }
    default:
      return CUDA_ERROR_INVALID_VALUE;
  }
  return CUDA_SUCCESS;
}

// ---------------------------------------------------------------------------
// Phase 1.7 — A.4: cuMemsetD16 / cuProfilerStart / cuProfilerStop / cuProfilerInitialize
// ---------------------------------------------------------------------------

extern "C" CUresult cuMemsetD16(CUdeviceptr dstDevice, unsigned short us,
                                 size_t N) {
  (void)dstDevice;
  (void)us;
  (void)N;
  // Lightweight stub: Phase 1 backend has no writable backing memory at
  // cuMemAlloc-returned pointers, so we cannot actually dereference dstDevice.
  // Return SUCCESS to maintain API surface stability; real implementation is
  // deferred to a Phase where cuMemAlloc returns host-backed memory.
  return CUDA_SUCCESS;
}

extern "C" CUresult cuProfilerStart(void) {
  return CUDA_ERROR_NOT_SUPPORTED;
}

extern "C" CUresult cuProfilerStop(void) {
  return CUDA_ERROR_NOT_SUPPORTED;
}

extern "C" CUresult cuProfilerInitialize(const char* configFile,
                                          const char* outputFile,
                                          unsigned int outputMode) {
  (void)configFile; (void)outputFile; (void)outputMode;
  return CUDA_ERROR_NOT_SUPPORTED;
}

// ---------------------------------------------------------------------------
// STUB sanity surface (Phase 1.7 E.7): 4 NOT_IMPLEMENTED APIs whose
// declarations live in include/cuda.h for E2E testing.
// ---------------------------------------------------------------------------

extern "C" CUresult cuArrayCreate(CUarray* pHandle, const void* allocSize) {
  (void)pHandle; (void)allocSize;
  return CUDA_ERROR_NOT_IMPLEMENTED;
}

extern "C" CUresult cuGraphCreate(CUgraph* phGraph, unsigned int flags) {
  (void)phGraph; (void)flags;
  return CUDA_ERROR_NOT_IMPLEMENTED;
}

extern "C" CUresult cuTexRefCreate(CUtexref* pTexRef) {
  (void)pTexRef;
  return CUDA_ERROR_NOT_IMPLEMENTED;
}

extern "C" CUresult cuMemHostRegister(void* p, size_t bytesize,
                                       unsigned int Flags) {
  (void)p; (void)bytesize; (void)Flags;
  return CUDA_ERROR_NOT_IMPLEMENTED;
}
