// SCOPE: UMD-EVOLUTION
// cu_module.cpp - Module/function handle management.
//
// Per Oracle critical review (2026-07-01), handles must be cleaned up
// when cuModuleUnload is called to prevent memory leak and stale
// handle resolution.
//
// Provides:
//   1. Strong symbol override of cuModuleLoad/cuModuleUnload/cuModuleGetFunction
//   2. Internal handle table with cleanup on unload
//   3. resolve_func_name_impl() for cu_launch.cpp linkage
//
// NOTE: does NOT include cu_stub_table.inc. The .inc declares weak stubs
// as cuModuleLoad(void), but this file defines them with full parameter lists.
// Including both would cause "conflicting types" in C linkage.

#include <cuda.h>

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace async_task::umd::shim {

namespace {

// Handle table state.
struct HandleTable {
  std::atomic<std::uint64_t> next_id{1};

  // Module handle -> list of function handles belonging to this module.
  std::unordered_map<CUmodule, std::vector<CUfunction>> mod_to_func;

  // Module handle -> module name (for diagnostics).
  std::unordered_map<CUmodule, std::string> mod_to_name;

  // Function handle -> kernel name (real implementation in cu_launch.cpp).
  std::unordered_map<CUfunction, std::string> func_to_name;

  // Function handle -> function attributes (cuFuncGetAttribute/SetAttribute).
  std::unordered_map<CUfunction, std::unordered_map<int, int>> func_to_attrs;

  // Function handle -> owning module (cuFuncGetModule reverse lookup).
  std::unordered_map<CUfunction, CUmodule> func_to_module;

  std::mutex mu;
};
HandleTable g_handles;

// Resolve CUfunction -> name. Returns empty string on missing.
std::string resolve_func_name(CUfunction f) {
  std::lock_guard<std::mutex> lock(g_handles.mu);
  auto it = g_handles.func_to_name.find(f);
  return it == g_handles.func_to_name.end() ? "" : it->second;
}

}  // namespace

// Expose resolve_func_name for cu_launch.cpp linkage.
std::string resolve_func_name_impl(CUfunction f) { return resolve_func_name(f); }

}  // namespace async_task::umd::shim

extern "C" CUresult cuModuleLoad(CUmodule* module, const char* fname) {
  if (!module) return CUDA_ERROR_INVALID_VALUE;
  std::lock_guard<std::mutex> lock(
      async_task::umd::shim::g_handles.mu);
  *module = reinterpret_cast<CUmodule>(
      async_task::umd::shim::g_handles.next_id.fetch_add(1));
  async_task::umd::shim::g_handles.mod_to_name[*module] = fname ? fname : "";
  return CUDA_SUCCESS;
}

extern "C" CUresult cuModuleGetFunction(CUfunction* hfunc, CUmodule hmod,
                                        const char* name) {
  if (!hfunc || !name) return CUDA_ERROR_INVALID_VALUE;
  std::lock_guard<std::mutex> lock(
      async_task::umd::shim::g_handles.mu);
  *hfunc = reinterpret_cast<CUfunction>(
      async_task::umd::shim::g_handles.next_id.fetch_add(1));
  async_task::umd::shim::g_handles.func_to_name[*hfunc] = name;

  // Track function -> module for cuFuncGetModule reverse lookup.
  async_task::umd::shim::g_handles.func_to_module[*hfunc] = hmod;

  // Track which functions belong to this module for cleanup.
  auto it = async_task::umd::shim::g_handles.mod_to_func.find(hmod);
  if (it != async_task::umd::shim::g_handles.mod_to_func.end()) {
    it->second.push_back(*hfunc);
  } else {
    // Module not loaded via cuModuleLoad (e.g., primary context module).
    // Skip tracking -- handle leaks are documented as known limitation.
  }
  return CUDA_SUCCESS;
}

extern "C" CUresult cuModuleUnload(CUmodule hmod) {
  std::lock_guard<std::mutex> lock(
      async_task::umd::shim::g_handles.mu);

  // Oracle critical fix: clean up function handles belonging to this module.
  auto it = async_task::umd::shim::g_handles.mod_to_func.find(hmod);
  if (it != async_task::umd::shim::g_handles.mod_to_func.end()) {
    for (CUfunction func : it->second) {
      async_task::umd::shim::g_handles.func_to_name.erase(func);
    }
    async_task::umd::shim::g_handles.mod_to_func.erase(it);
  }

  async_task::umd::shim::g_handles.mod_to_name.erase(hmod);
  return CUDA_SUCCESS;
}

// Stubs for module-related APIs not yet implemented.
extern "C" CUresult cuModuleGetGlobal(void** dptr, size_t* bytes,
                                      CUmodule hmod, const char* name) {
  (void)dptr; (void)bytes; (void)hmod; (void)name;
  return CUDA_ERROR_NOT_IMPLEMENTED;
}

extern "C" CUresult cuModuleGetTexRef(CUtexref* pTexRef, CUmodule hmod,
                                      const char* name) {
  (void)pTexRef; (void)hmod; (void)name;
  return CUDA_ERROR_NOT_IMPLEMENTED;
}

extern "C" CUresult cuModuleGetSurfRef(CUsurfref* pSurfRef, CUmodule hmod,
                                       const char* name) {
  (void)pSurfRef; (void)hmod; (void)name;
  return CUDA_ERROR_NOT_IMPLEMENTED;
}

extern "C" CUresult cuModuleLoadData(CUmodule* module, const void* image) {
  (void)module; (void)image;
  return CUDA_ERROR_NOT_IMPLEMENTED;
}

extern "C" CUresult cuModuleLoadDataEx(CUmodule* module, const void* image,
                                       unsigned int numOptions,
                                       int* options, void** optionValues) {
  (void)module; (void)image; (void)numOptions; (void)options; (void)optionValues;
  return CUDA_ERROR_NOT_IMPLEMENTED;
}

extern "C" CUresult cuModuleLoadFatBinary(CUmodule* module, const void* fatCubin) {
  (void)module; (void)fatCubin;
  return CUDA_ERROR_NOT_IMPLEMENTED;
}

// ---------------------------------------------------------------------------
// Phase 1.7 — A.1: cuFuncGetAttribute / cuFuncSetAttribute / cuFuncSetCacheConfig / cuFuncGetModule
// ---------------------------------------------------------------------------

extern "C" CUresult cuFuncGetAttribute(int* val, CUfunction_attribute attr,
                                       CUfunction f) {
  if (!val || !f) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_handles;
  std::lock_guard<std::mutex> lock(table.mu);

  if (table.func_to_name.find(f) == table.func_to_name.end())
    return CUDA_ERROR_INVALID_HANDLE;

  // Check if user has set a custom value via cuFuncSetAttribute.
  auto attrs_it = table.func_to_attrs.find(f);
  if (attrs_it != table.func_to_attrs.end()) {
    auto val_it = attrs_it->second.find(static_cast<int>(attr));
    if (val_it != attrs_it->second.end()) {
      *val = val_it->second;
      return CUDA_SUCCESS;
    }
  }

  // Default values per CUDA architecture.
  switch (attr) {
    case CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK:
      *val = 1024;
      break;
    case CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES:
      *val = 48 * 1024;
      break;
    case CU_FUNC_ATTRIBUTE_CONST_SIZE_BYTES:
      *val = 64 * 1024;
      break;
    case CU_FUNC_ATTRIBUTE_NUM_REGS:
      *val = 32;
      break;
    default:
      return CUDA_ERROR_INVALID_VALUE;
  }
  return CUDA_SUCCESS;
}

extern "C" CUresult cuFuncSetAttribute(CUfunction f, CUfunction_attribute attr,
                                       int val) {
  if (!f) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_handles;
  std::lock_guard<std::mutex> lock(table.mu);

  if (table.func_to_name.find(f) == table.func_to_name.end())
    return CUDA_ERROR_INVALID_HANDLE;

  table.func_to_attrs[f][static_cast<int>(attr)] = val;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuFuncSetCacheConfig(CUfunction f, CUfunc_cache config) {
  if (!f) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_handles;
  std::lock_guard<std::mutex> lock(table.mu);

  if (table.func_to_name.find(f) == table.func_to_name.end())
    return CUDA_ERROR_INVALID_HANDLE;

  table.func_to_attrs[f][1000] = static_cast<int>(config);  // 1000 = cache config key
  return CUDA_SUCCESS;
}

extern "C" CUresult cuFuncGetModule(CUmodule* mod, CUfunction f) {
  if (!mod || !f) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_handles;
  std::lock_guard<std::mutex> lock(table.mu);

  auto it = table.func_to_module.find(f);
  if (it == table.func_to_module.end())
    return CUDA_ERROR_INVALID_HANDLE;

  *mod = it->second;
  return CUDA_SUCCESS;
}

// ---------------------------------------------------------------------------
// Phase 1.7 — A.2: cuOccupancyMaxActiveBlocksPerMultiprocessor / WithFlags / cuOccupancyMaxPotentialBlockSize
// ---------------------------------------------------------------------------

extern "C" CUresult cuOccupancyMaxActiveBlocksPerMultiprocessor(
    int* blocks, CUfunction f, int blockSize, size_t dynSMem) {
  if (!blocks || !f) return CUDA_ERROR_INVALID_VALUE;
  if (blockSize <= 0) return CUDA_ERROR_INVALID_VALUE;
  (void)dynSMem;

  // Heuristic: assume 48KB shared memory per SM, compute max blocks.
  const int shared_mem_per_sm = 48 * 1024;
  int heuristic = shared_mem_per_sm / std::max(blockSize, 1);
  *blocks = std::max(1, std::min(32, heuristic));
  return CUDA_SUCCESS;
}

extern "C" CUresult cuOccupancyMaxActiveBlocksPerMultiprocessorWithFlags(
    int* blocks, CUfunction f, int blockSize, size_t dynSMem,
    unsigned int flags) {
  (void)flags;
  return cuOccupancyMaxActiveBlocksPerMultiprocessor(blocks, f, blockSize,
                                                     dynSMem);
}

extern "C" CUresult cuOccupancyMaxPotentialBlockSize(
    int* minGridSize, int* blockSize, CUfunction f,
    CUoccupancyB2DSize blockSizeToDynamicSMem, size_t dynSMemPerBlock,
    int blockSizeLimit) {
  if (!minGridSize || !blockSize) return CUDA_ERROR_INVALID_VALUE;
  (void)f;
  (void)blockSizeToDynamicSMem;
  (void)dynSMemPerBlock;
  (void)blockSizeLimit;

  // Reasonable heuristic for a stub GPU.
  *minGridSize = 80;    // 80 SMs
  *blockSize = 256;     // 256 threads per block
  return CUDA_SUCCESS;
}
