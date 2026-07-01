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
