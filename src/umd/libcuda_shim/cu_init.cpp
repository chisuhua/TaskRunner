// SCOPE: UMD-EVOLUTION
// cu_init.cpp - Shim initialization entry point (cuInit, runtime accessor)
//
// Provides:
//   1. async_task::umd::shim::runtime() — lazy singleton accessor for CudaRuntimeApi
//   2. cuInit() — strong symbol override of the weak stub from cu_stub_table.inc
//
// NOTE: does NOT include cu_stub_table.inc, because we define the strong symbol
// here with full parameter list, and the .inc declares it as (void) — which
// would cause conflicting types in C linkage.

#include "umd/cuda_runtime_api.hpp"
#include "test_fixture/cuda_scheduler.hpp"
#include "test_fixture/cuda_stub.hpp"
#include <cuda.h>

#include <atomic>
#include <memory>
#include <mutex>

namespace async_task::umd::shim {

namespace {
std::unique_ptr<taskrunner::CudaStub> g_stub;
std::unique_ptr<taskrunner::CudaScheduler> g_scheduler;
std::unique_ptr<CudaRuntimeApi> g_runtime;
std::once_flag g_init_flag;
}  // namespace

// Static ensure/runtime accessor for other cu* shim impls.
// Lazy initialization: creates standalone CudaStub + CudaScheduler + CudaRuntimeApi.
CudaRuntimeApi* runtime() {
  std::call_once(g_init_flag, []() {
    g_stub = std::make_unique<taskrunner::CudaStub>();
    g_scheduler = std::make_unique<taskrunner::CudaScheduler>(g_stub.get());
    g_scheduler->initialize(true);
    g_runtime = std::make_unique<CudaRuntimeApi>(g_scheduler.get());
  });
  return g_runtime.get();
}

}  // namespace async_task::umd::shim

extern "C" CUresult cuInit(unsigned int Flags) {
  (void)Flags;
  if (!async_task::umd::shim::runtime()) return CUDA_ERROR_UNKNOWN;
  return CUDA_SUCCESS;
}
