// SCOPE: UMD-EVOLUTION
/**
 * cuda_driver_accessor.hpp - Meyers-singleton fallback accessor for g_gpu_client
 *
 * Provides get_driver_or_default(): returns g_gpu_client if non-null, otherwise
 * a Meyers-singleton CudaStub instance. Does NOT mutate the global pointer.
 *
 * Rationale: shim functions (cuStreamSynchronize, cuGraphLaunch, cuMemPoolCreate)
 * historically returned CUDA_ERROR_NOT_INITIALIZED when g_gpu_client == nullptr,
 * forcing every test to explicitly set g_gpu_client = &g_mock; before invocation.
 *
 * This header preserves the production CLI path (init_gpu_client() in
 * gpu_driver_client.cpp still creates GpuDriverClient when null), but allows
 * shim-only tests to skip the explicit setup.
 *
 * Created: 2026-07-11 (g-gpu-client-meyers-singleton-fallback)
 */

#pragma once

#include "test_fixture/cuda_stub.hpp"
#include "test_fixture/gpu_driver_client.h"

namespace async_task::umd::shim {

// Returns g_gpu_client if non-null, otherwise a Meyers-singleton CudaStub.
// Does NOT mutate the global g_gpu_client pointer.
//
// Marked inline so multiple translation units (cu_stream.cpp, cu_graph.cpp,
// cu_mem_pool.cpp) can include this without multiple-definition errors.
inline async_task::gpu::IGpuDriver* get_driver_or_default() {
  if (async_task::gpu::g_gpu_client != nullptr) {
    return async_task::gpu::g_gpu_client;
  }
  // Meyers singleton fallback — constructed on first call, never destroyed,
  // never assigned to g_gpu_client.
  static async_task::gpu::CudaStub default_stub;
  return &default_stub;
}

}  // namespace async_task::umd::shim
