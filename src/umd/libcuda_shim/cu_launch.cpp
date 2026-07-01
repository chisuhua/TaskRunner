// SCOPE: UMD-EVOLUTION
// cu_launch.cpp - cuLaunchKernel implementation for LD_PRELOAD shim.
//
// Resolves CUfunction -> kernel name via cu_module.cpp's handle table
// (resolve_func_name_impl), then delegates to Phase 1's CudaRuntimeApi::launch_kernel.
//
// Provides:
//   1. cuLaunchKernel — primary kernel launch entry point
//   2. cuLaunchKernelEx — extended config launch (CUlaunchConfig defined locally)
//   3. cuLaunchHostFunc — NOT_IMPLEMENTED (deferred)
//   4. cuLaunchCooperativeKernel — delegates to cuLaunchKernel
//
// NOTE: The compat cuda.h at /tmp/cuda_compat/ is minimal and does not define
// CUlaunchConfig or CUhostFn. These types are defined locally below.
//
// See design doc: docs/superpowers/specs/2026-06-30-umd-evolution-redesign.md

#include <cuda.h>
#include "umd/cuda_runtime_api.hpp"

#include <cstddef>

namespace async_task::umd::shim {
extern CudaRuntimeApi* runtime();
extern std::string resolve_func_name_impl(CUfunction f);
}  // namespace async_task::umd::shim

using async_task::umd::CudaError;
using async_task::umd::Dim3;

// ---------------------------------------------------------------------------
// Locally-defined types missing from compat cuda.h
// ---------------------------------------------------------------------------

// CUDA Driver API launch config — now defined in include/cuda.h.
// This file relies on that definition.
typedef void (*CUhostFn)(void* userData);

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

inline CUresult to_cuda_error(CudaError err) {
  switch (err) {
    case CudaError::Success:      return CUDA_SUCCESS;
    case CudaError::InvalidValue: return CUDA_ERROR_INVALID_VALUE;
    case CudaError::NotSupported: return CUDA_ERROR_NOT_SUPPORTED;
    case CudaError::OutOfMemory:  return CUDA_ERROR_OUT_OF_MEMORY;
    case CudaError::Unknown:      return CUDA_ERROR_UNKNOWN;
    default:                      return CUDA_ERROR_UNKNOWN;
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// cuLaunchKernel — primary kernel launch
// ---------------------------------------------------------------------------

extern "C" CUresult cuLaunchKernel(CUfunction f,
                                   unsigned int gridDimX,
                                   unsigned int gridDimY,
                                   unsigned int gridDimZ,
                                   unsigned int blockDimX,
                                   unsigned int blockDimY,
                                   unsigned int blockDimZ,
                                   unsigned int sharedMemBytes,
                                   CUstream hStream,
                                   void** kernelParams,
                                   void** extra) {
  (void)hStream;
  (void)extra;

  if (!f) return CUDA_ERROR_INVALID_HANDLE;

  // Resolve CUfunction -> kernel name.
  std::string name = async_task::umd::shim::resolve_func_name_impl(f);
  if (name.empty()) return CUDA_ERROR_INVALID_HANDLE;

  Dim3 grid{gridDimX, gridDimY, gridDimZ};
  Dim3 block{blockDimX, blockDimY, blockDimZ};

  // Phase 2 limitation: kernelParams/extra are NOT serialized. Real CUDA
  // Driver API requires marshaling args into a kernarg buffer; Phase 2 PoC
  // routes args directly through CudaRuntimeApi::launch_kernel, which
  // calls CudaScheduler::submit_launch with LaunchParams.
  auto err = async_task::umd::shim::runtime()->launch_kernel(
      name, grid, block, kernelParams, sharedMemBytes);
  return to_cuda_error(err);
}

// ---------------------------------------------------------------------------
// cuLaunchKernelEx — extended config (basic fields only, Phase 2)
// ---------------------------------------------------------------------------

extern "C" CUresult cuLaunchKernelEx(const CUlaunchConfig* config,
                                     CUfunction f,
                                     void** extraParams) {
  if (!config) return CUDA_ERROR_INVALID_VALUE;
  if (!f) return CUDA_ERROR_INVALID_HANDLE;
  (void)extraParams;

  // Resolve CUfunction -> kernel name.
  std::string name = async_task::umd::shim::resolve_func_name_impl(f);
  if (name.empty()) return CUDA_ERROR_INVALID_HANDLE;

  Dim3 grid{config->gridDimX, config->gridDimY, config->gridDimZ};
  Dim3 block{config->blockDimX, config->blockDimY, config->blockDimZ};

  // Phase 2 limitation: CUlaunchConfig params/attrs fields are not
  // extracted. Basic grid/block/sharedMem/stream fields are forwarded.
  // extraParams is not processed.
  auto err = async_task::umd::shim::runtime()->launch_kernel(
      name, grid, block, nullptr, config->sharedMemBytes);
  return to_cuda_error(err);
}

// ---------------------------------------------------------------------------
// cuLaunchHostFunc — deferred
// ---------------------------------------------------------------------------

extern "C" CUresult cuLaunchHostFunc(CUstream hStream,
                                     CUhostFn fn,
                                     void* userData) {
  (void)hStream;
  (void)fn;
  (void)userData;
  return CUDA_ERROR_NOT_IMPLEMENTED;
}

// ---------------------------------------------------------------------------
// cuLaunchCooperativeKernel — delegates to cuLaunchKernel
// (cooperative semantics not enforced in Phase 2)
// ---------------------------------------------------------------------------

extern "C" CUresult cuLaunchCooperativeKernel(CUfunction f,
                                              unsigned int gridDimX,
                                              unsigned int gridDimY,
                                              unsigned int gridDimZ,
                                              unsigned int blockDimX,
                                              unsigned int blockDimY,
                                              unsigned int blockDimZ,
                                              unsigned int sharedMemBytes,
                                              CUstream hStream,
                                              void** kernelParams) {
  // Delegate to cuLaunchKernel (cooperative semantics not enforced).
  return cuLaunchKernel(
      f, gridDimX, gridDimY, gridDimZ,
      blockDimX, blockDimY, blockDimZ,
      sharedMemBytes, hStream,
      kernelParams, nullptr);
}
