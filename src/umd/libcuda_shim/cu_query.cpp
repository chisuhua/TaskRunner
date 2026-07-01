// SCOPE: UMD-EVOLUTION
// cu_query.cpp - Critical query APIs that libcudart.so and basic CUDA
// programs rely on.
//
// Per Oracle Critical Finding #2 (2026-07-01), these APIs MUST return
// CUDA_SUCCESS or CUDA_ERROR_NOT_SUPPORTED — never crash.
//
// NOTE: cuDeviceGetName, cuDeviceGetAttribute, cuDeviceTotalMem live in
// cu_device.cpp (C.5). Do NOT duplicate them here.

#include <cuda.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" CUresult cuDriverGetVersion(int* version) {
  if (!version) return CUDA_ERROR_INVALID_VALUE;
  *version = 12000;  // CUDA 12.0 (matches CUDA Toolkit family in use)
  return CUDA_SUCCESS;
}

extern "C" CUresult cuDriverGet(int* driver) {
  if (!driver) return CUDA_ERROR_INVALID_VALUE;
  *driver = 12000;  // Same version as cuDriverGetVersion
  return CUDA_SUCCESS;
}

// Error handling utilities (called by libcudart.so for error → string).
extern "C" CUresult cuGetErrorName(CUresult error, const char** pstr) {
  if (!pstr) return CUDA_ERROR_INVALID_VALUE;
  switch (error) {
    case CUDA_SUCCESS:
      *pstr = "cudaSuccess";
      break;
    case CUDA_ERROR_OUT_OF_MEMORY:
      *pstr = "cudaErrorOutOfMemory";
      break;
    case CUDA_ERROR_INVALID_VALUE:
      *pstr = "cudaErrorInvalidValue";
      break;
    case CUDA_ERROR_NOT_SUPPORTED:
      *pstr = "cudaErrorNotSupported";
      break;
    case CUDA_ERROR_INVALID_HANDLE:
      *pstr = "cudaErrorInvalidHandle";
      break;
    // CUDA_ERROR_NOT_IMPLEMENTED is aliased to CUDA_ERROR_NOT_SUPPORTED (both 801)
    default:
      *pstr = "cudaErrorUnknown";
      break;
  }
  return CUDA_SUCCESS;
}

extern "C" CUresult cuGetErrorString(CUresult error, const char** pstr) {
  if (!pstr) return CUDA_ERROR_INVALID_VALUE;
  switch (error) {
    case CUDA_SUCCESS:
      *pstr = "no error";
      break;
    case CUDA_ERROR_OUT_OF_MEMORY:
      *pstr = "out of memory";
      break;
    case CUDA_ERROR_INVALID_VALUE:
      *pstr = "invalid argument";
      break;
    case CUDA_ERROR_NOT_SUPPORTED:
      *pstr = "operation not supported";
      break;
    case CUDA_ERROR_INVALID_HANDLE:
      *pstr = "invalid handle";
      break;
    // CUDA_ERROR_NOT_IMPLEMENTED is aliased to CUDA_ERROR_NOT_SUPPORTED (both 801)
    default:
      *pstr = "unknown error";
      break;
  }
  return CUDA_SUCCESS;
}

// cuDevicePrimaryCtx* — primary context management.
// In Phase 2 PoC: always return device 0's implicit context.
// Uses a sentinel handle (0x1) that cuCtxDestroy treats as a valid context.
extern "C" CUresult cuDevicePrimaryCtxRetain(CUcontext* pctx, CUdevice dev) {
  if (!pctx) return CUDA_ERROR_INVALID_VALUE;
  if (dev != 0) return CUDA_ERROR_INVALID_VALUE;
  // Phase 2 PoC: primary context is a fixed sentinel handle (0x1).
  // cuCtx* in cu_ctx.cpp know about this (next_id starts at 2, reserving 1).
  *pctx = reinterpret_cast<CUcontext>(static_cast<std::uintptr_t>(0x1));
  return CUDA_SUCCESS;
}

extern "C" CUresult cuDevicePrimaryCtxRelease(CUdevice dev) {
  if (dev != 0) return CUDA_ERROR_INVALID_VALUE;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuDevicePrimaryCtxReset(CUdevice dev) {
  if (dev != 0) return CUDA_ERROR_INVALID_VALUE;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuDevicePrimaryCtxSetFlags(CUdevice dev,
                                               unsigned int flags) {
  if (dev != 0) return CUDA_ERROR_INVALID_VALUE;
  (void)flags;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuDevicePrimaryCtxGetState(CUdevice dev,
                                                unsigned int* flags,
                                                int* active) {
  if (!flags || !active) return CUDA_ERROR_INVALID_VALUE;
  if (dev != 0) return CUDA_ERROR_INVALID_VALUE;
  *flags = 0;
  *active = 1;
  return CUDA_SUCCESS;
}
