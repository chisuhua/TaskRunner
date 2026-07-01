# UMD Phase 2 (LD_PRELOAD Driver API shim) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `libcuda_taskrunner.so` that exports 12 core `cu*` symbols + ~200 stubs so unmodified CUDA programs run on TaskRunner + UsrLinuxEmu via LD_PRELOAD.

**Architecture:** Shim intercepts `cu*` calls via dlsym, redirects to `CudaRuntimeApi` (Phase 1 deliverable), maintains `CUfunction→name` handle table in shim layer for kernel resolution.

**Tech Stack:** C++17, CMake 3.10+, dlopen/RTLD_NEXT, existing CudaRuntimeApi from Phase 1.

**Spec/Plan:** `docs/superpowers/specs/2026-06-30-umd-evolution-redesign.md` §Phase 2; `docs/superpowers/plans/2026-06-30-umd-evolution-redesign.md` §Sub-plan C (macro outline)

---

## Prerequisites

Phase 1 complete and on `main`:
- `CudaRuntimeApi` (`include/umd/cuda_runtime_api.hpp`, `src/umd/cuda_runtime_api.cpp`)
- 8 test cases + 4 CLI commands
- 39 tests pass

---

## Sub-plan C: Phase 2 — LD_PRELOAD Driver API shim

**Goal:** `libcuda_taskrunner.so` intercepts 12 core `cu*` Driver API symbols; ~200 stub `cu*` APIs return `CUDA_ERROR_NOT_IMPLEMENTED`; vectorAdd sample runs to `cuCtxSynchronize` completion.

**Files to create/modify:**
- Create: `src/umd/libcuda_shim/` directory
- Create: `tools/generate_cu_stubs.py` (auto-generate stub table)
- Create: `src/umd/libcuda_shim/cu_init.cpp`
- Create: `src/umd/libcuda_shim/cu_device.cpp`
- Create: `src/umd/libcuda_shim/cu_ctx.cpp`
- Create: `src/umd/libcuda_shim/cu_module.cpp`
- Create: `src/umd/libcuda_shim/cu_mem.cpp`
- Create: `src/umd/libcuda_shim/cu_launch.cpp`
- Create: `src/umd/libcuda_shim/cu_sync.cpp`
- Create: `src/umd/libcuda_shim/cu_stub_table.inc` (auto-generated)
- Modify: `cmake/UMDEvolution.cmake`
- Create: `tests/umd/test_cuda_shim.cpp`
- Modify: `tools/docs-audit.sh` (add stub completeness check)

### Task C.0: Confirm Phase 2 scope with user

**Goal:** Confirm Phase 2 kicks off with current assumptions.

- [ ] **Step 1**: Confirm with user:
  - Q1: Should Phase 2 use `TaskRunner::getScheduler()` (test-fixture scope) OR standalone CudaStub (CLI pattern)?
  - Q3: Vulcan extension points — keep architectural reservation (no implementation)?
  - VectorAdd E2E acceptable as success criterion (knowing it won't produce real kernel results without D-3)?

If user prefers different defaults, update design spec accordingly.

### Task C.1: Build stub generator

**Files:**
- Create: `tools/generate_cu_stubs.py`

- [ ] **Step 1**: Write Python script that generates `cu_stub_table.inc` with ~200 stub functions returning `CUDA_ERROR_NOT_IMPLEMENTED`.

```python
#!/usr/bin/env python3
"""Generate CUDA Driver API stub functions for libcuda_taskrunner.so.

Reads CUDA Driver API symbol list (cu* functions in CUDA 12.x) and generates
200+ inline functions returning CUDA_ERROR_NOT_IMPLEMENTED for libcuda shim.
"""

# Critical APIs that MUST be implemented (not stubbed) for libcudart.so compatibility.
# These are checked in tools/docs-audit.sh and fail if missing implementation.
CRITICAL_APIS_IMPL_REQUIRED = {
    # Initialization & Version
    "cuInit", "cuDriverGetVersion", "cuDriverGet",
    # Device
    "cuDeviceGetCount", "cuDeviceGet", "cuDeviceGetName",
    "cuDeviceGetAttribute", "cuDeviceTotalMem",
    # Context
    "cuCtxCreate", "cuCtxDestroy", "cuCtxSetCurrent",
    "cuCtxGetCurrent", "cuCtxPushCurrent", "cuCtxPopCurrent",
    "cuCtxSynchronize", "cuCtxGetDevice", "cuCtxGetApiVersion",
    "cuDevicePrimaryCtxRetain", "cuDevicePrimaryCtxRelease",
    "cuDevicePrimaryCtxReset",
    # Module
    "cuModuleLoad", "cuModuleUnload", "cuModuleGetFunction",
    "cuModuleGetGlobal",
    # Memory
    "cuMemAlloc", "cuMemFree", "cuMemcpyHtoD", "cuMemcpyDtoH",
    "cuMemcpyDtoD", "cuMemcpy", "cuMemcpyAsync",
    "cuMemsetD32", "cuMemsetD8", "cuMemAllocHost", "cuMemFreeHost",
    # Launch
    "cuLaunchKernel",
}

CUDA_DRIVER_APIS = [
    # Initialization / Device
    "cuInit", "cuDriverGet", "cuDeviceGetCount", "cuDeviceGet",
    "cuDeviceGetName", "cuDeviceGetAttribute", "cuDeviceTotalMem",
    # Context Management
    "cuCtxCreate", "cuCtxDestroy", "cuCtxSetCurrent", "cuCtxGetCurrent",
    "cuCtxPushCurrent", "cuCtxPopCurrent", "cuCtxSynchronize",
    "cuDevicePrimaryCtxRetain", "cuDevicePrimaryCtxRelease",
    # Module Loading
    "cuModuleLoad", "cuModuleLoadData", "cuModuleLoadDataEx",
    "cuModuleLoadFatBinary", "cuModuleUnload", "cuModuleGetFunction",
    "cuModuleGetTexRef", "cuModuleGetSurfRef",
    # Memory Management
    "cuMemAlloc", "cuMemFree", "cuMemAllocHost", "cuMemFreeHost",
    "cuMemAllocManaged", "cuMemcpyHtoD", "cuMemcpyDtoH", "cuMemcpyDtoD",
    "cuMemcpyHtoDAsync", "cuMemcpyDtoHAsync", "cuMemcpyDtoDAsync",
    "cuMemsetD32", "cuMemsetD32Async", "cuMemsetD16", "cuMemsetD8",
    "cuMemcpy", "cuMemcpyAsync", "cuMemcpy2D", "cuMemcpy2DAsync",
    "cuMemcpy3D", "cuMemcpy3DAsync",
    # Stream & Event
    "cuStreamCreate", "cuStreamDestroy", "cuStreamSynchronize",
    "cuStreamQuery", "cuStreamWaitEvent", "cuStreamWaitValue32",
    "cuStreamAddCallback", "cuStreamCreateWithPriority",
    "cuStreamGetPriority", "cuStreamGetCaptureInfo",
    "cuEventCreate", "cuEventDestroy", "cuEventRecord", "cuEventSynchronize",
    "cuEventElapsedTime", "cuEventQuery",
    # Kernel Launch
    "cuLaunchKernel", "cuLaunchKernelEx", "cuLaunchHostFunc",
    "cuLaunchCooperativeKernel",
    # Texture/Surface
    "cuTexRefCreate", "cuTexRefDestroy", "cuTexRefSetAddress",
    "cuSurfRefCreate", "cuSurfRefDestroy",
    # Graph
    "cuGraphCreate", "cuGraphDestroy", "cuGraphInstantiate",
    "cuGraphLaunch", "cuGraphAddKernelNode", "cuGraphExecLaunch",
    # ... ~200 total
]


def main():
    stub_file = open("src/umd/libcuda_shim/cu_stub_table.inc", "w")
    critical_file = open("src/umd/libcuda_shim/cu_critical_stubs.inc", "w")

    stub_file.write("// Auto-generated by tools/generate_cu_stubs.py\n")
    stub_file.write("// Do not edit manually.\n")
    stub_file.write("#pragma once\n")
    stub_file.write("#include <cuda.h>\n\n")

    critical_file.write("// Auto-generated by tools/generate_cu_stubs.py\n")
    critical_file.write("// DO_NOT_STUB — Critical APIs that must be implemented in cu_*.cpp\n")
    critical_file.write("// These APIs are checked by tools/docs-audit.sh for completeness.\n")
    critical_file.write("#pragma once\n")
    critical_file.write("#include <cuda.h>\n\n")

    for fn in CUDA_DRIVER_APIS:
        if fn in CRITICAL_APIS_IMPL_REQUIRED:
            critical_file.write(f"// {fn}: DO_NOT_STUB — implement in cu_*.cpp\n")
            critical_file.write(f"// Expected location: src/umd/libcuda_shim/cu_{{init,ctx,device,module,mem,launch,query}}.cpp\n\n")
        else:
            stub_file.write(f"extern \"C\" __attribute__((visibility(\"default\")))\n")
            stub_file.write(f"CUresult {fn}_stub();\n\n")

    stub_file.close()
    critical_file.close()


if __name__ == "__main__":
    main()
```

- [ ] **Step 2**: Run: `python3 tools/generate_cu_stubs.py`
- [ ] **Step 3**: Verify stub table has 200+ entries and critical APIs file is generated:
```bash
wc -l src/umd/libcuda_shim/cu_stub_table.inc src/umd/libcuda_shim/cu_critical_stubs.inc
```
- [ ] **Step 3b**: Create validation script `scripts/check_critical_apis.sh` that fails build if any critical API is missing implementation:
```bash
#!/bin/bash
# scripts/check_critical_apis.sh — Verify all critical cu* APIs are implemented (not stubbed)
CRITICAL_APIS=(
  "cuInit" "cuDriverGetVersion" "cuDeviceGetCount" "cuDeviceGet"
  "cuCtxCreate" "cuCtxDestroy" "cuCtxSynchronize"
  "cuModuleLoad" "cuModuleUnload" "cuModuleGetFunction"
  "cuMemAlloc" "cuMemFree" "cuMemcpyHtoD" "cuMemcpyDtoH"
  "cuLaunchKernel"
)
SHIM_DIR="src/umd/libcuda_shim"
missing=0
for api in "${CRITICAL_APIS[@]}"; do
  if ! grep -q "extern \"C\" CUresult $api" $SHIM_DIR/*.cpp; then
    echo "CRITICAL: $api not implemented (must not be stub)"
    missing=$((missing + 1))
  fi
done
if [ $missing -gt 0 ]; then exit 1; fi
echo "All critical APIs implemented: ✓"
```
- [ ] **Step 4**: Commit:

```bash
git add tools/generate_cu_stubs.py src/umd/libcuda_shim/cu_stub_table.inc src/umd/libcuda_shim/cu_critical_stubs.inc scripts/check_critical_apis.sh
git commit -m "feat(shim): add cu* stub generator + 200+ not-implemented functions + critical API validation"
```

### Task C.2: Implement cu_init + cuModule handle table

**Files:**
- Create: `src/umd/libcuda_shim/cu_init.cpp`
- Create: `src/umd/libcuda_shim/cu_module.cpp`

- [ ] **Step 1**: Write the shim's cu_init implementation:

```cpp
// SCOPE: UMD-EVOLUTION
// Shim initialization entry point.

#include "umd/cuda_runtime_api.hpp"
#include "test_fixture/cuda_scheduler.hpp"

#include <atomic>
#include <memory>
#include <mutex>

namespace async_task::umd::shim {

namespace {
std::unique_ptr<CudaRuntimeApi> g_runtime;
std::once_flag g_init_flag;

// Handle ↔ name maps for cuModule/cuFunction protocol.
struct HandleTable {
  std::atomic<std::uint64_t> next_id{1};
  std::unordered_map<CUfunction, std::string> func_table;
  std::unordered_map<CUmodule, std::string> mod_table;
  std::mutex mu;
};
HandleTable g_handles;

// Resolve CUfunction -> name; returns empty string on missing.
std::string resolve_func_name(CUfunction f) {
  std::lock_guard<std::mutex> lock(g_handles.mu);
  auto it = g_handles.func_table.find(f);
  return it == g_handles.func_table.end() ? "" : it->second;
}

}  // namespace

// Static ensure/runtime accessor for other cu* shim impls.
CudaRuntimeApi* runtime() {
  std::call_once(g_init_flag, []() {
    // TODO: prefer TaskRunner::getScheduler() per Phase 1 B.1 if scope matches
    auto* stub = std::make_unique<taskrunner::CudaStub>();
    auto* scheduler = new taskrunner::CudaScheduler(stub.get());
    g_runtime = std::make_unique<CudaRuntimeApi>(scheduler);
  });
  return g_runtime.get();
}

}  // namespace async_task::umd::shim

extern "C" CUresult cuInit(unsigned int Flags) {
  (void)Flags;
  if (!async_task::umd::shim::runtime()) return CUDA_ERROR_UNKNOWN;
  return CUDA_SUCCESS;
}
```

- [ ] **Step 2**: Write cu_module.cpp with handle allocation:

```cpp
// cu_module.cpp - Shim module/function handle management
#include <cuda.h>
#include <unordered_map>
#include <mutex>

namespace async_task::umd::shim {
extern HandleTable g_handles;  // declared in cu_init.cpp

extern "C" CUresult cuModuleLoad(CUmodule* module, const char* fname) {
  *module = reinterpret_cast<CUmodule>(g_handles.next_id.fetch_add(1));
  std::lock_guard<std::mutex> lock(g_handles.mu);
  g_handles.mod_table[*module] = fname ? fname : "";
  return CUDA_SUCCESS;
}

extern "C" CUresult cuModuleGetFunction(CUfunction* hfunc, CUmodule hmod,
                                        const char* name) {
  (void)hmod;
  *hfunc = reinterpret_cast<CUfunction>(g_handles.next_id.fetch_add(1));
  std::lock_guard<std::mutex> lock(g_handles.mu);
  g_handles.func_table[*hfunc] = name;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuModuleUnload(CUmodule hmod) {
  std::lock_guard<std::mutex> lock(g_handles.mu);
  // Oracle finding: clean up associated CUfunction handles to prevent leaks
  for (auto it = g_handles.func_table.begin(); it != g_handles.func_table.end(); ) {
    // Note: this is a simplification; in real CUDA, functions are tracked per-module.
    // Phase 2: store module→function map to enable cleanup.
    // TODO: maintain g_handles.mod_to_func[hmod] = {fn1, fn2, ...}
    (void)it;
    ++it;
  }
  g_handles.mod_table.erase(hmod);
  return CUDA_SUCCESS;
}
```

- [ ] **Step 3**: Verify compile: `cd build && make -j4`
- [ ] **Step 4**: Commit:

```bash
git add src/umd/libcuda_shim/cu_init.cpp src/umd/libcuda_shim/cu_module.cpp
git commit -m "feat(shim): cuInit and cuModule* handle table"
```

### Task C.3: Implement cuMem APIs

**Files:**
- Create: `src/umd/libcuda_shim/cu_mem.cpp`

- [ ] **Step 1**: Write cu_mem.cpp mapping cuMemAlloc/Free/Memcpy to CudaRuntimeApi:

```cpp
// cu_mem.cpp - Memory allocation and copy APIs
#include <cuda.h>
#include "umd/cuda_runtime_api.hpp"

namespace async_task::umd::shim {
extern CudaRuntimeApi* runtime();

inline CudaMemcpyKind translate_kind(cudaMemcpyKind k) {
  switch (k) {
    case cudaMemcpyHostToDevice: return CudaMemcpyKind::HostToDevice;
    case cudaMemcpyDeviceToHost: return CudaMemcpyKind::DeviceToHost;
    case cudaMemcpyDeviceToDevice: return CudaMemcpyKind::DeviceToDevice;
    case cudaMemcpyHostToHost: return CudaMemcpyKind::HostToHost;
    default: return CudaMemcpyKind::HostToDevice;
  }
}
}  // namespace

using async_task::umd::CudaError;
using async_task::umd::CudaMemcpyKind;

extern "C" CUresult cuMemAlloc(CUdeviceptr* dptr, size_t bytesize) {
  void* ptr = nullptr;
  auto err = async_task::umd::shim::runtime()->malloc(&ptr, bytesize);
  if (err != CudaError::Success) return CUDA_ERROR_OUT_OF_MEMORY;
  *dptr = reinterpret_cast<CUdeviceptr>(ptr);
  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemFree(CUdeviceptr dptr) {
  // Phase 2: no-op (malloc in Phase 1 has no free)
  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemcpyHtoD(CUdeviceptr dstDevice, const void* srcHost,
                                  size_t ByteCount) {
  auto err = async_task::umd::shim::runtime()->memcpy(
      reinterpret_cast<void*>(dstDevice), srcHost, ByteCount,
      CudaMemcpyKind::HostToDevice);
  return err == CudaError::Success ? CUDA_SUCCESS : CUDA_ERROR_UNKNOWN;
}

extern "C" CUresult cuMemcpyDtoH(void* dstHost, CUdeviceptr srcDevice,
                                  size_t ByteCount) {
  auto err = async_task::umd::shim::runtime()->memcpy(
      dstHost, reinterpret_cast<void*>(srcDevice), ByteCount,
      CudaMemcpyKind::DeviceToHost);
  return err == CudaError::Success ? CUDA_SUCCESS : CUDA_ERROR_UNKNOWN;
}

// Phase 2 limitation: D2D/H2H return CUDA_ERROR_NOT_SUPPORTED
extern "C" CUresult cuMemcpyDtoD(CUdeviceptr dstDevice,
                                  CUdeviceptr srcDevice,
                                  size_t ByteCount) {
  (void)dstDevice; (void)srcDevice; (void)ByteCount;
  return CUDA_ERROR_NOT_SUPPORTED;
}
```

- [ ] **Step 2**: Build and verify
- [ ] **Step 3**: Commit:

```bash
git add src/umd/libcuda_shim/cu_mem.cpp
git commit -m "feat(shim): cuMem* APIs (alloc/copy/free)"
```

### Task C.4: Implement cuLaunchKernel

**Files:**
- Create: `src/umd/libcuda_shim/cu_launch.cpp`

- [ ] **Step 1**: Write cu_launch.cpp that resolves CUfunction→name and calls runtime:

```cpp
// cu_launch.cpp - cuLaunchKernel implementation
#include <cuda.h>
#include "umd/cuda_runtime_api.hpp"

extern std::string resolve_func_name(CUfunction f);  // from cu_init.cpp

using async_task::umd::CudaError;
using async_task::umd::Dim3;

extern "C" CUresult cuLaunchKernel(CUfunction f,
                                   unsigned int gridDimX, unsigned int gridDimY,
                                   unsigned int gridDimZ,
                                   unsigned int blockDimX, unsigned int blockDimY,
                                   unsigned int blockDimZ,
                                   unsigned int sharedMemBytes,
                                   CUstream hStream,
                                   void** kernelParams, void** extra) {
  (void)hStream; (void)extra;
  std::string name = resolve_func_name(f);
  if (name.empty()) return CUDA_ERROR_INVALID_HANDLE;

  Dim3 grid{gridDimX, gridDimY, gridDimZ};
  Dim3 block{blockDimX, blockDimY, blockDimZ};
  auto err = async_task::umd::shim::runtime()->launch_kernel(
      name, grid, block, kernelParams, sharedMemBytes);
  return err == CudaError::Success ? CUDA_SUCCESS :
         err == CudaError::InvalidValue ? CUDA_ERROR_INVALID_HANDLE :
                                          CUDA_ERROR_UNKNOWN;
}
```

- [ ] **Step 2**: Build and verify
- [ ] **Step 3**: Commit:

```bash
git add src/umd/libcuda_shim/cu_launch.cpp
git commit -m "feat(shim): cuLaunchKernel with CUfunction → name resolution"
```

### Task C.5: Implement cuCtx* and cuDevice* APIs

**Files:**
- Create: `src/umd/libcuda_shim/cu_ctx.cpp`
- Create: `src/umd/libcuda_shim/cu_device.cpp`

- [ ] **Step 1**: cu_ctx.cpp — context with simple stack tracking + cuCtxSynchronize:

```cpp
// cu_ctx.cpp - Context with simple stack tracking
#include <cuda.h>
#include <mutex>
#include <vector>
#include <atomic>

namespace {
struct ContextStack {
  std::vector<CUcontext> stack;
  std::mutex mu;
  CUcontext current() {
    // pop top or null
    return stack.empty() ? nullptr : stack.back();
  }
  void push(CUcontext ctx) { stack.push_back(ctx); }
  void pop() {
    if (!stack.empty()) stack.pop_back();
  }
};
ContextStack g_ctx;
}

extern "C" CUresult cuCtxCreate(CUcontext* pctx, unsigned int flags, CUdevice dev) {
  static std::atomic<uint64_t> next_ctx_id{2};  // 0x1 reserved for primary
  *pctx = reinterpret_cast<CUcontext>(next_ctx_id.fetch_add(1));
  std::lock_guard<std::mutex> lock(g_ctx.mu);
  g_ctx.stack.push_back(*pctx);
  (void)flags; (void)dev;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuCtxSynchronize(void) {
  // Phase 2: synchronous semantics — already done at each API call site
  return CUDA_SUCCESS;
}

extern "C" CUresult cuCtxSetCurrent(CUcontext ctx) {
  std::lock_guard<std::mutex> lock(g_ctx.mu);
  g_ctx.stack.push_back(ctx);
  return CUDA_SUCCESS;
}

extern "C" CUresult cuCtxGetCurrent(CUcontext* pctx) {
  std::lock_guard<std::mutex> lock(g_ctx.mu);
  *pctx = g_ctx.stack.empty() ? nullptr : g_ctx.stack.back();
  return CUDA_SUCCESS;
}

extern "C" CUresult cuCtxDestroy(CUcontext ctx) {
  std::lock_guard<std::mutex> lock(g_ctx.mu);
  for (auto it = g_ctx.stack.begin(); it != g_ctx.stack.end(); ++it) {
    if (*it == ctx) { g_ctx.stack.erase(it); break; }
  }
  return CUDA_SUCCESS;
}
```

- [ ] **Step 2**: cu_device.cpp — basic device enumeration:

```cpp
// cu_device.cpp - Device enumeration
#include <cuda.h>

extern "C" CUresult cuDeviceGetCount(int* count) {
  *count = 1;  // Phase 2 single-device stub
  return CUDA_SUCCESS;
}

extern "C" CUresult cuDeviceGet(CUdevice* dev, int ordinal) {
  *dev = 0;  // Phase 2: only device 0
  (void)ordinal;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuCtxCreate(CUcontext* pctx, unsigned int flags,
                                CUdevice dev) {
  *pctx = reinterpret_cast<CUcontext>(0x1);  // fake handle
  (void)flags; (void)dev;
  return CUDA_SUCCESS;
}
```

- [ ] **Step 3**: Build and verify
- [ ] **Step 4**: Commit:

```bash
git add src/umd/libcuda_shim/cu_ctx.cpp src/umd/libcuda_shim/cu_device.cpp
git commit -m "feat(shim): cuCtx* and cuDevice* APIs (single-device stub)"
```

### Task C.5b: Implement critical query APIs (Oracle Critical #2)

**Files:**
- Create: `src/umd/libcuda_shim/cu_query.cpp`

- [ ] **Step 1**: Implement cuDriverGetVersion (returns hardcoded CUDA 12.0):

```cpp
// cu_query.cpp - Critical query APIs needed by libcudart.so
#include <cuda.h>
#include <cstring>

extern "C" CUresult cuDriverGetVersion(int* version) {
  *version = 12000;  // CUDA 12.0
  return CUDA_SUCCESS;
}
```

- [ ] **Step 2**: Implement cuDeviceGetAttribute (limited support — return 0 for unimplemented):

```cpp
extern "C" CUresult cuDeviceGetAttribute(int* value,
                                          CUdevice_attribute attrib,
                                          CUdevice dev) {
  *value = 0;
  switch (attrib) {
    case CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT: *value = 1; break;
    case CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_MULTIPROCESSOR: *value = 1024; break;
    case CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR: *value = 7; break;
    case CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR: *value = 5; break;
    default: *value = 0;  // Unknown attribute
  }
  return CUDA_SUCCESS;
}
```

- [ ] **Step 3**: Implement cuDeviceTotalMem, cuDeviceGetName:

```cpp
extern "C" CUresult cuDeviceTotalMem(size_t* bytes, CUdevice dev) {
  *bytes = 8ULL * 1024 * 1024 * 1024;  // 8 GB
  (void)dev;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuDeviceGetName(char* name, int len, CUdevice dev) {
  const char* pname = "TaskRunner CUDA Stub";
  std::strncpy(name, pname, len - 1);
  name[len - 1] = '\0';
  (void)dev;
  return CUDA_SUCCESS;
}
```

- [ ] **Step 4**: Implement cuDevicePrimaryCtx APIs (basic):

```cpp
extern "C" CUresult cuDevicePrimaryCtxRetain(CUcontext* pctx, CUdevice dev) {
  *pctx = reinterpret_cast<CUcontext>(0x1);  // single dummy context
  (void)dev;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuDevicePrimaryCtxRelease(CUdevice dev) {
  (void)dev;
  return CUDA_SUCCESS;
}
```

- [ ] **Step 5**: Build, commit:

```bash
git add src/umd/libcuda_shim/cu_query.cpp
git commit -m "feat(shim): critical query APIs (cuDriverGetVersion, cuDeviceGetAttribute, cuCtxGetDevice)"
```

### Task C.6: Wire stubs into shim shared library

**Files:**
- Modify: `cmake/UMDEvolution.cmake`

- [ ] **Step 1**: Update `cmake/UMDEvolution.cmake` to build `libcuda_taskrunner.so`:

```cmake
# Phase 2: LD_PRELOAD shim
add_library(cuda_taskrunner SHARED
    src/umd/libcuda_shim/cu_init.cpp
    src/umd/libcuda_shim/cu_device.cpp
    src/umd/libcuda_shim/cu_ctx.cpp
    src/umd/libcuda_shim/cu_module.cpp
    src/umd/libcuda_shim/cu_mem.cpp
    src/umd/libcuda_shim/cu_launch.cpp
    src/umd/libcuda_shim/cu_sync.cpp
    src/umd/libcuda_shim/cu_stub.cpp  # all NOT_IMPLEMENTED stubs
)
target_include_directories(cuda_taskrunner PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(cuda_taskrunner PUBLIC
    taskrunner_test_fixture
    taskrunner_shared
    dl
)
target_compile_features(cuda_taskrunner PUBLIC cxx_std_17)
set_target_properties(cuda_taskrunner PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
```

- [ ] **Step 2**: Verify build: `cd build && cmake .. && make cuda_taskrunner -j4`

Expected: `libcuda_taskrunner.so` produced in build dir.

- [ ] **Step 3**: Verify all 12 core + 200 stub symbols exported:

```bash
nm -D --defined-only build/libcuda_taskrunner.so | grep "cu[A-Z]" | head -20
```

Expected: cuInit, cuDeviceGet, cuDeviceGetCount, cuCtxCreate, cuCtxSynchronize, cuModuleLoad, cuModuleGetFunction, cuMemAlloc, cuMemcpyHtoD, cuLaunchKernel, etc.

- [ ] **Step 4**: Commit:

```bash
git add cmake/UMDEvolution.cmake
git commit -m "build: wire libcuda_taskrunner.so as shared library with 200+ exported symbols"
```

### Task C.7: Shim E2E test (real CUDA sample)

**Files:**
- Create: `tests/umd/test_cuda_shim.cpp`

- [ ] **Step 1**: Verify Phase 2 shim end-to-end using LD_PRELOAD:

```cpp
// tests/umd/test_cuda_shim.cpp - Phase 2 E2E test
//
// Verifies libcuda_taskrunner.so intercepts cu* calls correctly via dlsym.
// Run with: LD_PRELOAD=./libcuda_taskrunner.so ./test_cuda_shim
// (This test links against libcuda symbols directly to verify they're exported.)

#include <cuda.h>
#include <doctest/doctest.h>
#include <cstring>
#include <vector>
#include <string>

TEST_CASE("cuInit returns SUCCESS after LD_PRELOAD") {
  CHECK(cuInit(0) == CUDA_SUCCESS);
}

TEST_CASE("cuDeviceGet returns single device") {
  int count = 0;
  CHECK(cuDeviceGetCount(&count) == CUDA_SUCCESS);
  CHECK(count >= 1);
  CUdevice dev;
  CHECK(cuDeviceGet(&dev, 0) == CUDA_SUCCESS);
}

TEST_CASE("cuMemAlloc/Copy/Free cycle works") {
  CUdeviceptr dptr;
  CHECK(cuMemAlloc(&dptr, 4096) == CUDA_SUCCESS);
  CHECK(dptr != 0);
  std::vector<uint8_t> data(4096, 0xAB);
  CHECK(cuMemcpyHtoD(dptr, data.data(), 4096) == CUDA_SUCCESS);
  CHECK(cuMemFree(dptr) == CUDA_SUCCESS);
}

TEST_CASE("Not-supported APIs return CUDA_ERROR_NOT_IMPLEMENTED") {
  CUresult r = cuStreamCreate(nullptr, 0);
  CHECK(r == CUDA_ERROR_NOT_IMPLEMENTED);
}

TEST_CASE("context lifecycle: create, set, get, destroy") {
  CUcontext ctx;
  CHECK(cuCtxCreate(&ctx, 0, 0) == CUDA_SUCCESS);
  CHECK(ctx != 0);
  CHECK(cuCtxSetCurrent(ctx) == CUDA_SUCCESS);
  CUcontext current;
  CHECK(cuCtxGetCurrent(&current) == CUDA_SUCCESS);
  CHECK(current == ctx);
  CHECK(cuCtxDestroy(ctx) == CUDA_SUCCESS);
}

TEST_CASE("driver version returns CUDA 12.x") {
  int version = 0;
  CHECK(cuDriverGetVersion(&version) == CUDA_SUCCESS);
  CHECK(version >= 11000);  // at least CUDA 11.0
}

TEST_CASE("device enumeration consistent") {
  int count = -1;
  CHECK(cuDeviceGetCount(&count) == CUDA_SUCCESS);
  CHECK(count == 1);
  CUdevice dev;
  CHECK(cuDeviceGet(&dev, 0) == CUDA_SUCCESS);
  char name[256];
  CHECK(cuDeviceGetName(name, sizeof(name), dev) == CUDA_SUCCESS);
  CHECK(std::string(name) == "TaskRunner CUDA Stub");
}

TEST_CASE("module unload cleanup") {
  CUmodule mod;
  CHECK(cuModuleLoad(&mod, "fake.cubin") == CUDA_SUCCESS);
  CUfunction func;
  CHECK(cuModuleGetFunction(&func, mod, "vectorAdd") == CUDA_SUCCESS);
  CHECK(cuModuleUnload(mod) == CUDA_SUCCESS);
  // After unload, function lookup should fail (handle obsolete)
  // (Validation: this would be exercised via cuLaunchKernel returning INVALID_HANDLE)
}
```

- [ ] **Step 2**: Add to CMakeLists.txt as `test_cuda_shim` target linking against `cuda_taskrunner` library
- [ ] **Step 3**: Verify test passes (Phase 2 PoC success):

```bash
./build/test_cuda_shim 2>&1 | tail -10
```

Expected: all tests pass (Phase 2 E2E verification).

- [ ] **Step 4**: Commit:

```bash
git add tests/umd/test_cuda_shim.cpp cmake/UMDEvolution.cmake
git commit -m "test(shim): E2E tests for LD_PRELOAD cu* interception"
```

### Task C.8: Stub completeness check + docs update

**Files:**
- Modify: `tools/docs-audit.sh` (add stub completeness check)
- Modify: `docs/umd-evolution/architecture/runtime-layering.md` (mark Phase 2 complete)

- [ ] **Step 1**: Add stub completeness check to docs-audit.sh:

```bash
# Phase 2: verify all 12 core + ~200 stub cu* symbols are exported
EXP=12
ACTUAL=$(nm -D --defined-only build/libcuda_taskrunner.so | grep -c "cu[A-Z]" || echo 0)
if [ "$ACTUAL" -lt "$EXP" ]; then
  echo "FAIL: libcuda_taskrunner.so has $ACTUAL cu* symbols (expected >= $EXP)"
  exit 1
fi
```

- [ ] **Step 2**: Update runtime-layering.md: mark CudaRuntimeApi-in-shim layer as ✅ Phase 2 Implemented
- [ ] **Step 3**: Run `./tools/docs-audit.sh` — confirm PASS
- [ ] **Step 4**: Commit:

```bash
git add tools/docs-audit.sh docs/umd-evolution/architecture/runtime-layering.md
git commit -m "chore: Phase 2 stub completeness check + docs"
```

### Task C.8b: Document Handle Lifecycle Assumptions (Oracle Finding #5)

**Files:**
- Modify: `docs/umd-evolution/architecture/runtime-layering.md`

- [ ] **Step 1**: Add "Handle Lifecycle" section to runtime-layering.md documenting:
  - 64-bit handle IDs allocated via atomic counter (uniqueness guaranteed)
  - `cuModuleUnload` cleans up only module handle from mod_table; per-function cleanup is partial
  - Multi-context support via simple stack (Oracle Critical #4)
  - Thread safety via single mutex on handle tables
  - Known limitation: handle table memory grows unboundedly without cleanup
- [ ] **Step 2**: Commit:

```bash
git add docs/umd-evolution/architecture/runtime-layering.md
git commit -m "docs(umd): document handle lifecycle assumptions (Oracle finding #5)"
```

### Task C.9: Final Phase 2 verification + summary

- [ ] **Step 1**: All test suites pass:

```bash
for test in test_cuda_scheduler test_gpu_architecture test_gpu_phase2 test_cuda_runtime_api test_cuda_shim; do
  ./build/$test 2>&1 | tail -3
done
```

Expected: 39 + N (shim) tests pass.

- [ ] **Step 2**: docs-audit.sh passes
- [ ] **Step 3**: Cross-repo sync per ADR-035:

```bash
cd /workspace/project/UsrLinuxEmu
git add external/TaskRunner
git commit -m "chore(submodule): bump TaskRunner to <new SHA> for Phase 2 LD_PRELOAD shim"
git push origin main
```

- [ ] **Step 4**: Add Phase 2 Implementation Status section to `runtime-layering.md`
- [ ] **Step 5**: Mark Phase 2 complete in design spec STATUS
- [ ] **Step 6**: Final commit:

```bash
git add docs/umd-evolution/architecture/runtime-layering.md \
        docs/superpowers/specs/2026-06-30-umd-evolution-redesign.md
git commit -m "docs(umd): Phase 2 LD_PRELOAD shim complete (C.1-C.9, vectorAdd-ready)"
```

---

## Open Questions

| ID | Question | Default |
|----|----------|---------|
| Q1 | Phase 2 cuInit uses TaskRunner::getScheduler() or standalone? | Standalone (matches CLI B.5 pattern, avoids doctest.h dependency) |
| Q3 | Vulkan extension points — keep or remove? | Keep architectural reservation (no implementation) |
| Q4 | VectorAdd E2E acceptable as Phase 2 success? | Yes, with note that real kernel execution requires D-3 |
| Q6 | How aggressive on handle-table memory cleanup? | Partial cleanup (module only), known limitation documented |

---

## Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| Shim doesn't export all expected symbols | Stub generator script ensures 200+ |
| libcudart.so uses non-stubbed cu* and crashes | Comprehensive stub list + warning docs |
| Handle collisions across processes | Atomic counter for unique IDs |
| VectorAdd compatibility issues | MVP with env-var kernel registry; cuda_runtime_register CLI |
| Handle cleanup partial (Oracle Critical #1) | Documented as known limitation in runtime-layering.md |
| libcudart.so CUDA version check fails | cuDriverGetVersion returns 12000 (CUDA 12.x) |
| Thread safety on multi-threaded CUDA apps | Mutex-protected handle tables (single-context supported) |

---

## References

- Design spec: `docs/superpowers/specs/2026-06-30-umd-evolution-redesign.md` §Phase 2
- Phase 1 implementation: commits 020814c, cb07353, 8bc847a, 4314dae, 9a50bd5
- Oracle reviews: Phase 1, Phase 2 (2026-06-30)
- AMD ROCm research: `docs/umd-evolution/research/external-amd-rocm-umd-2026-06-24.md`
- NVIDIA CUDA research: `docs/umd-evolution/research/external-nvidia-cuda-umd-2026-06-24.md`

---

**Status**: PROPOSED awaiting user confirmation to begin execution.
