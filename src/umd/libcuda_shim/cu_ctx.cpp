// SCOPE: UMD-EVOLUTION
// cu_ctx.cpp - Context management with stack tracking.
//
// Oracle critical fix (2026-07-01): replaces hardcoded 0x1 handle with
// stack-tracked context list. cuCtxSetCurrent/GetCurrent/push/pop
// all participate in this thread-local context stack.

#include <cuda.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

// Missing types from compat cuda.h (Phase 2 PoC: int-sized enum compatibility).
// Phase 1.7: cuda.h provides proper CUfunc_cache enum, CUsharedconfig/CUlimit int typedefs.

// Cache config storage (accessed by extern "C" cuCtxGet/SetCacheConfig).
static CUfunc_cache g_cache_config = CU_FUNC_CACHE_PREFER_NONE;

namespace async_task::umd::shim {

namespace {

// Per-instance context state.
struct ContextState {
  std::atomic<std::uint64_t> next_id{2};  // 0=invalid, 1=primary reserved

  // All contexts ever created (for cleanup lookup).
  std::unordered_map<CUcontext, std::uint64_t> created;

  // Global mutex for context map mutations.
  std::mutex mu;
};
ContextState g_ctx;

// Thread-local context stack (true CUDA semantics).
thread_local std::vector<CUcontext> tls_stack;

CUcontext new_context_id() {
  std::lock_guard<std::mutex> lock(g_ctx.mu);
  return reinterpret_cast<CUcontext>(
      static_cast<std::uintptr_t>(g_ctx.next_id.fetch_add(1)));
}

void mark_created(CUcontext c) {
  std::lock_guard<std::mutex> lock(g_ctx.mu);
  g_ctx.created[c] = 1;
}

void mark_destroyed(CUcontext c) {
  std::lock_guard<std::mutex> lock(g_ctx.mu);
  g_ctx.created.erase(c);
}

bool exists(CUcontext c) {
  std::lock_guard<std::mutex> lock(g_ctx.mu);
  return g_ctx.created.count(c) > 0;
}

}  // namespace

}  // namespace async_task::umd::shim

extern "C" CUresult cuCtxCreate(CUcontext* pctx, unsigned int flags,
                                CUdevice dev) {
  if (!pctx) return CUDA_ERROR_INVALID_VALUE;
  *pctx = async_task::umd::shim::new_context_id();
  async_task::umd::shim::mark_created(*pctx);
  async_task::umd::shim::tls_stack.push_back(*pctx);
  (void)flags;
  (void)dev;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuCtxDestroy(CUcontext ctx) {
  if (!ctx) return CUDA_ERROR_INVALID_VALUE;
  if (!async_task::umd::shim::exists(ctx)) return CUDA_ERROR_INVALID_HANDLE;

  // Remove from this thread's stack if present.
  auto& stack = async_task::umd::shim::tls_stack;
  for (auto it = stack.begin(); it != stack.end(); ++it) {
    if (*it == ctx) {
      stack.erase(it);
      break;
    }
  }

  async_task::umd::shim::mark_destroyed(ctx);
  return CUDA_SUCCESS;
}

extern "C" CUresult cuCtxSetCurrent(CUcontext ctx) {
  if (ctx && !async_task::umd::shim::exists(ctx))
    return CUDA_ERROR_INVALID_HANDLE;
  auto& stack = async_task::umd::shim::tls_stack;
  stack.clear();
  if (ctx) stack.push_back(ctx);
  return CUDA_SUCCESS;
}

extern "C" CUresult cuCtxGetCurrent(CUcontext* pctx) {
  if (!pctx) return CUDA_ERROR_INVALID_VALUE;
  auto& stack = async_task::umd::shim::tls_stack;
  *pctx = stack.empty() ? nullptr : stack.back();
  return CUDA_SUCCESS;
}

extern "C" CUresult cuCtxPushCurrent(CUcontext ctx) {
  if (ctx && !async_task::umd::shim::exists(ctx))
    return CUDA_ERROR_INVALID_HANDLE;
  auto& stack = async_task::umd::shim::tls_stack;
  stack.push_back(ctx);
  return CUDA_SUCCESS;
}

extern "C" CUresult cuCtxPopCurrent(CUcontext* pctx) {
  auto& stack = async_task::umd::shim::tls_stack;
  if (stack.empty()) {
    // Compat cuda.h does not define CUDA_ERROR_INVALID_CONTEXT.
    // Use INVALID_HANDLE as closest approximation.
    return CUDA_ERROR_INVALID_HANDLE;
  }
  if (pctx) *pctx = stack.back();
  stack.pop_back();
  return CUDA_SUCCESS;
}

extern "C" CUresult cuCtxSynchronize(void) {
  // Phase 2: synchronous semantics — already done at each API call site.
  // cuCtxSynchronize is a no-op (Phase 2 PoC).
  return CUDA_SUCCESS;
}

extern "C" CUresult cuCtxGetDevice(CUdevice* device) {
  if (!device) return CUDA_ERROR_INVALID_VALUE;
  *device = 0;  // Phase 2: single device
  return CUDA_SUCCESS;
}

extern "C" CUresult cuCtxGetFlags(unsigned int* flags) {
  if (!flags) return CUDA_ERROR_INVALID_VALUE;
  *flags = 0;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuCtxGetApiVersion(CUcontext ctx, unsigned int* version) {
  if (!version) return CUDA_ERROR_INVALID_VALUE;
  *version = 12000;  // CUDA 12.0
  (void)ctx;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuCtxGetCacheConfig(CUfunc_cache* pconfig) {
  if (!pconfig) return CUDA_ERROR_INVALID_VALUE;
  *pconfig = g_cache_config;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuCtxSetCacheConfig(CUfunc_cache config) {
  g_cache_config = config;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuCtxGetSharedMemConfig(CUsharedconfig* pConfig) {
  if (!pConfig) return CUDA_ERROR_INVALID_VALUE;
  // Default: 4-byte bank size (CU_SHARED_MEM_CONFIG_DEFAULT_BANK_SIZE in real CUDA).
  *pConfig = 0;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuCtxSetSharedMemConfig(CUsharedconfig config) {
  (void)config;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuCtxGetLimit(size_t* pvalue, CUlimit limit) {
  if (!pvalue) return CUDA_ERROR_INVALID_VALUE;
  // Phase 2 PoC: return reasonable defaults per limit type.
  switch (limit) {
    case CU_LIMIT_STACK_SIZE:
      *pvalue = 1024;
      break;
    case CU_LIMIT_PRINTF_FIFO_SIZE:
      *pvalue = 1048576;
      break;
    case CU_LIMIT_MALLOC_HEAP_SIZE:
      *pvalue = 8388608;
      break;
    default:
      *pvalue = 0;
      break;
  }
  return CUDA_SUCCESS;
}

extern "C" CUresult cuCtxSetLimit(CUlimit limit, size_t value) {
  (void)limit;
  (void)value;
  return CUDA_SUCCESS;
}
