---
SCOPE: UMD-EVOLUTION
STATUS: ACCEPTED
DESIGN_DATE: 2026-06-30
DESIGN_AUTHOR: Sisyphus (with Oracle architectural review)
Q4_RESOLVED: 2026-06-30 (POA-1 + POA-2 dual motivation)
PHASE_2_STATUS: ACCEPTED (2026-07-01, libcuda_taskrunner.so + 37 tests)
RELATED: tadr-201, tadr-202, tadr-203, tadr-204, tadr-205
---

# UMD-EVOLUTION Redesign (CUDA Runtime API + LD_PRELOAD Driver API shim)

> **Naming convention**: This spec replaces the original `umd-evolution` vision-source.md with a phased, deliverable-focused roadmap. Vulkan is **out of scope**; architecture reserves extension points only.

## Executive Summary

UMD-EVOLUTION rebuilds from a vision-only scope with internal contradictions to a 4-phase executable plan:

| Phase | Duration | Deliverable | Cross-repo Dep |
|-------|----------|------------|----------------|
| **Phase 0** (Doc Fix) | 0.5 w | Cleaned docs, architecture/ + README updated, tadr state reconciled | None |
| **Phase 1** (Runtime PoC) | 2-3 w | `CudaRuntimeApi` wrapping `CudaScheduler`, 3 CUDA Runtime APIs (cudaMalloc/Memcpy/LaunchKernel) verified end-to-end | None |
| **Phase 2** (LD_PRELOAD shim) | 2-3 w | `libcuda_taskrunner.so`, 12 cu* APIs core + ~200 stubs, vectorAdd E2E | None |
| **Phase 3** (API extension) | 3-5 w | Stream, Event, Memory pool, YAML cubin registry | Minor |

**Total**: 6-9 weeks (excluding optional Phase 3 ELF parsing).

**Trigger gate**: Phase 1 starts only after (a) Phase 0 complete, (b) explicit user PoC requirement confirmed, (c) test-fixture scope stable (34/34 ✅). Required condition (b) is currently undefined.

## Goals

1. Provide a minimal CUDA Runtime API surface (cudaMalloc / cudaMemcpy / cudaLaunchKernel) as PoC
2. Implement `LD_PRELOAD` shim for CUDA Driver API (cu*) so unmodified CUDA programs run on TaskRunner + UsrLinuxEmu
3. Build on top of proven IGpuDriver abstraction (31 methods, 3 DI implementations verified working)
4. Preserve Vulkan as architectural extension point, but no implementation

## Non-Goals

- Full CUDA Runtime API coverage (Phase 1: 3 APIs only)
- Full CUDA Driver API coverage (Phase 2: 12 core APIs, ~200 stubs)
- Real cubin/.nv.info parsing (deferred to tadr-205 D-3, infinite phase)
- Vulkan Runtime API (architectural reservation only)
- C++ template support (HIP-style)
- Cross-platform (Linux x86_64 only)

## Architecture

### Component Stack (cumulative view)

```
┌─────────────────────────────────────────────────────┐
│  Phase 2+ : Real CUDA Program (LD_PRELOAD)          │
│  (e.g. NVIDIA CUDA Samples vectorAdd)               │
└──────────────────────┬──────────────────────────────┘
                       │ libcudart.so + intercepted cu*
┌──────────────────────▼──────────────────────────────┐
│  Phase 2 : libcuda_taskrunner.so [NEW]              │
│                                                     │
│  12 core cu* symbols:                               │
│    cuInit, cuDeviceGetCount, cuDeviceGet,            │
│    cuCtxCreate, cuModuleLoad, cuModuleGetFunction,  │
│    cuMemAlloc, cuMemcpyHtoD, cuLaunchKernel,        │
│    cuCtxSynchronize, cuMemcpyDtoH, cuMemFree        │
│  ~200 stub cu* symbols: return CUDA_ERROR_NOT_IMPL  │
└──────────────────────┬──────────────────────────────┘
                       │ Phase 1 internal calls
┌──────────────────────▼──────────────────────────────┐
│  Phase 1 : CudaRuntimeApi [REPLACES skeleton]       │
│                                                     │
│  - DI: takes CudaScheduler* (single point of entry) │
│  - ctor: scheduler->driver()->create_va_space()     │
│          + create_queue()                           │
│  - dtor: reverse teardown (RAII-safe on failure)    │
│  - 3 APIs: cudaMalloc / cudaMemcpy / cudaLaunchKern │
│  - Default synchronous semantics (wait_fence)      │
│  - kernel_registry_: name→index map (CudaStub ABI)  │
│  - std::mutex mu_ protects handles/registry         │
│  - D2D/H2H memcpy: returns cudaErrorNotSupported   │
└──────────────────────┬──────────────────────────────┘
                       │ CudaScheduler high-level ops
┌──────────────────────▼──────────────────────────────┐
│  CudaScheduler (existing, H-3/H-5 verified)         │
│                                                     │
│  submit_mem_alloc(size)                             │
│  submit_memcpy_h2d(s,d,n) / submit_memcpy_d2h(...)  │
│  submit_launch(LaunchParams)                        │
│  wait_fence(fence_id)                               │
└──────────────────────┬──────────────────────────────┘
                       │ IGpuDriver* (DI)
┌──────────────────────▼──────────────────────────────┐
│  IGpuDriver (31 methods)                            │
│  ├─ GpuDriverClient (real ioctl → UsrLinuxEmu)      │
│  ├─ CudaStub (fake) ← ⚠️ Phase 1 only mode       │
│  └─ MockGpuDriver (test mock)                       │
└─────────────────────────────────────────────────────┘
```

### Why this architecture (vs alternatives)

| Alternative | Why rejected |
|-------------|-------------|
| Direct `CudaRuntimeApi → IGpuDriver` (skip CudaScheduler) | Re-implements MemoryManager + fence glue; introduces abstraction leakage. Oracle review caught the IGpuDriver signature mismatch. |
| LLVM/Clang-style IR translation | Out of scope for Phase 0-3; adds 4-6 weeks. |
| Replace CudaScheduler with new UnifiedScheduler | Superseded by tadr-109 (IGpuDriver DI), code is in main. |
| libcudart.so shim instead of libcuda.so shim | Real CUDA programs link libcudart.so which then calls libcuda.so. Standard practice: intercept at libcuda.so layer. |

## Phase 0: Documentation Fix (Pre-requisite)

### 0.1 TADR state reconciliation

| TADR | Current | After Phase 0 | Body addition |
|------|---------|---------------|---------------|
| tadr-201 | `PROPOSED` + body "Accepted retroactive" | `SUPERSEDED` | Link to tadr-109: "UnifiedScheduler not implemented, replaced by IGpuDriver DI" |
| tadr-202 | `PROPOSED` + body "Accepted retroactive" | `SUPERSEDED` | Add "CommandTranslator not implemented; CudaScheduler.encode_gpfifo_entry() substituted" |
| tadr-203 | `PROPOSED` + body "Accepted retroactive" | `SUPERSEDED` | Add "SyncSource/SyncManager deferred; fence_id mechanism (H-3 S3.5) substitutes" |

### 0.2 Architecture directory creation

```
docs/umd-evolution/architecture/
├── README.md          # Architecture overview + component diagram
└── runtime-layering.md  # Phase 1/2 layering design
```

Content extracted from this design doc + tadr-105/109.

### 0.3 Conflict resolution

In `docs/umd-evolution/README.md` add priority table:

| Conflict | Priority |
|----------|----------|
| vision.md vs gap-analysis.md | **gap-analysis.md wins** (ROI analysis is decisive) |
| TADR vs actual code | **Code wins** (code in `main` is canonical) |
| vision-source.md vs vision.md | **vision.md wins** (vision-source is DEPRECATED reference only) |

### 0.4 tadr-205 dependency analysis added

Append to tadr-205:

| Phase | Cross-repo dep | Blocking? |
|-------|----------------|-----------|
| D-1 | UsrLinuxEmu doorbell mmap (ADR-024) | 🔴 Major |
| D-2 | None (IGpuDriver methods sufficient) | No |
| D-3 | UsrLinuxEmu BasicGpuSimulator kernel exec | 🔴 Major |

Kernel launch dependency cycle: D-2 cudaLaunchKernel → relies on D-3 ELF parsing + Sim exec → therefore **D-2 PoC only runs via CudaStub mode**; real execution requires D-3 prerequisite.

## Phase 1: Runtime PoC Architecture

### Interface (corrected per Oracle review)

```cpp
// SCOPE: UMD-EVOLUTION
// include/umd/cuda_api.hpp (replaces existing skeleton)

namespace async_task::cuda {

class CudaRuntimeApi {
public:
  explicit CudaRuntimeApi(CudaScheduler* scheduler);  // DI, non-singleton
  ~CudaRuntimeApi();

  // Kernel registration (manual, Phase 1 only)
  void register_kernel(const std::string& name, uint32_t index);

  // 3 PoC APIs (default synchronous)
  cudaError_t malloc(void** devPtr, size_t size);
  cudaError_t memcpy(void* dst, const void* src, size_t count,
                      cudaMemcpyKind kind);  // H2D/D2H only
  cudaError_t launch_kernel(const std::string& name,
                              dim3 gridDim, dim3 blockDim,
                              void** args, size_t sharedMem,
                              cudaStream_t stream = nullptr);

private:
  CudaScheduler* scheduler_;
  uint64_t va_space_handle_;
  uint64_t queue_handle_;
  std::unordered_map<std::string, uint32_t> kernel_registry_;
  std::mutex mu_;
};

}  // namespace async_task::cuda
```

### Path mapping (verified against actual IGpuDriver methods)

| Public API | CudaScheduler call | Why (vs direct IGpuDriver) |
|------------|-------------------|---------------------------|
| `cudaMalloc(size)` | `scheduler_->submit_mem_alloc(size)` | sched combines `MemoryManager.allocate()` + `sync::SyncManager.create_fence()` |
| `cudaMemcpy(H2D)` | `scheduler_->submit_memcpy_h2d(src, dst, n)` | adds fence create + serialization |
| `cudaMemcpy(D2H)` | `scheduler_->submit_memcpy_d2h(src, dst, n)` | same |
| `cudaLaunchKernel(name, g, b, args, shmem)` | `scheduler_->submit_launch(LaunchParams{g,b,args,shmem})` | LaunchParams handles kernel arg serialization |
| Sync (default) | `scheduler_->wait_fence(fence_id)` | mandatory before returning success |
| Init (ctor) | `scheduler_->driver()->create_va_space(0)` + `create_queue(...)` | H-3 Phase 2 verified |
| Cleanup (dtor) | reverse teardown via RAII | tested fail-safety |

### Phase 1 known limitations

| Limit | Reason | Phase 2+ |
|-------|--------|----------|
| GpuDriverClient backend returns `-ENOSYS` | CudaScheduler uses `dynamic_cast<CudaStub*>` at 5 call sites | Cleanup track (out of Phase 0-3) |
| D2D/H2H memcpy not supported | `gpu_ioctl.h` only defines H2D/D2H paths | Adapter layer (Phase 3.2) |
| Single stream, no `cuStreamCreate` | PoC scope | Stream support (Phase 3.1) |
| Kernel names manually registered | No ELF parsing | YAML registry (Phase 3.3) or ELF (tadr-205 D-3) |
| No `cuModuleLoad` | cuModule out of PoC scope | Phase 2 shim |

### Testing (8 mandatory cases)

| Test | Validates |
|------|-----------|
| `malloc_free_roundtrip` | RAII safety + handle teardown |
| `memcpy_h2d_data_integrity` | Data path correctness |
| `memcpy_d2h_data_integrity` | Bidirectional consistency |
| `memcpy_d2d_returns_not_supported` | Graceful error for unsupported kind |
| `launch_kernel_returns_fence` | Synchronization completion |
| `register_kernel_duplicate_detection` | Registry integrity |
| `ctor_fail_no_va_space_leak` | Resource cleanup on init failure |
| `multi_thread_concurrent_alloc` | Mutex correctness |

## Phase 2: LD_PRELOAD Driver API shim

### Symbol surface

**12 core cu* APIs** (full replacement, no RTLD_NEXT chain):

```
cuInit, cuDeviceGetCount, cuDeviceGet, cuCtxCreate,
cuModuleLoad, cuModuleGetFunction, cuMemAlloc,
cuMemcpyHtoD, cuLaunchKernel, cuCtxSynchronize,
cuMemcpyDtoH, cuMemFree
```

**~200 stub cu* APIs**: return `CUDA_ERROR_NOT_IMPLEMENTED`. Auto-generated template; refer to NVIDIA libcuda.so symbol table for completeness.

### CUfunction handle strategy

shim maintains `std::unordered_map<CUfunction, std::string>` table; handle IDs from atomic counter. `cuModuleLoad` returns fake CUmodule handle; `cuModuleGetFunction` allocates CUfunction and stores name; `cuLaunchKernel` resolves CUfunction→name→Phase 1 path.

### cuInit implementation pattern

```cpp
extern "C" CUresult cuInit(unsigned int Flags) {
  static std::once_flag init_flag;
  std::call_once(init_flag, []() {
    auto& runner = TaskRunner::getInstance();
    runner.initialize();
    auto* scheduler = runner.get_scheduler();  // NEW accessor (Phase 1)
    g_runtime = std::make_unique<CudaRuntimeApi>(scheduler);
    // Pre-register kernels (PoC MVP)
    if (const char* env = std::getenv("CUDA_KERNEL_REGISTRY")) {
      // parse "name1:index,name2:index" format
    }
  });
  return CUDA_SUCCESS;
}
```

### Build & deploy

```cmake
# cmake/UMDEvolution.cmake
add_library(cuda_taskrunner SHARED
  src/umd/libcuda_shim/cu_init.cpp
  src/umd/libcuda_shim/cu_mem.cpp
  src/umd/libcuda_shim/cu_launch.cpp
  src/umd/libcuda_shim/cu_sync.cpp
)
target_link_libraries(cuda_taskrunner PRIVATE
  taskrunner_test_fixture
  dl
)
```

Usage: `LD_PRELOAD=./libcuda_taskrunner.so ./vectorAdd`

### Phase 2 testing

| Test | Validates |
|------|-----------|
| VectorAdd E2E (LD_PRELOAD) | Shim sym resolution + libcudart.so compatibility |
| Symbol table completeness | All ~200 cu* symbols exported, none crash link |
| Dual cuInit idempotency | std::call_once guarantees |
| Handle collision across cuModuleLoad calls | Atomic counter uniqueness |

### Phase 2 known limitations (explicit in docs)

- Stubbed APIs return `CUDA_ERROR_NOT_IMPLEMENTED` (no chain to real libcuda.so)
- Multi-device: `cuDeviceGetCount` returns 1 regardless of host config
- Version symbols (`cuInit_v2`) need stub templates; binutils nm-extracted from real libcuda.so

## Phase 2 Implementation Status (ACCEPTED 2026-07-01)

Phase 2 (LD_PRELOAD Driver API shim) is implemented on `main` branch.

### Deliverables (11 commits, 2026-07-01)

| Task | Description | Commit |
|------|-------------|--------|
| C.1 | `tools/generate_cu_stubs.py` + 143 cu\* declarations | `a2cfe36` |
| C.2 | `cu_init.cpp` + `cu_module.cpp` (Oracle cleanup) | `e7741ec` |
| C.3 | `cu_mem.cpp` (cuMem\*) | `a12e9e9` |
| C.4 | `cu_launch.cpp` (handle→name resolution) | `a7beaac` |
| C.5 | `cu_ctx.cpp` + `cu_device.cpp` (Oracle stack) | `e2fdf51` |
| C.5b | `cu_query.cpp` + `cu_stream.cpp` + `cu_event.cpp` | `12e69a4` |
| C.6 | `libcuda_taskrunner.so` linked (79 cu\* exported) | `07cbb50` |
| C.7 | `test_cuda_shim.cpp` (37/37 passing) | `afb00f1` |
| C.8 | stub completeness check + status docs | `6cbcd56` |
| C.8b | handle lifecycle documentation | `17de516` |
| C.9 | final verification (this section) | (current commit) |

### Test Results

- **Phase 1 tests**: 39/39 pass (8 + 11 + 12 + 8)
- **Phase 2 shim tests**: 37/37 pass
- **Total**: 76/76 pass, 0 failures

### Symbol Coverage

- **79 cu\* symbols** exported by `libcuda_taskrunner.so`
- **41/41 critical APIs** implemented (100% coverage)
- **C.5b additions**: cuDriverGetVersion=12000, cuDevicePrimaryCtx\*, cuGetErrorName/String

### Constraints per Phase 2 Limitations Documented in `runtime-layering.md`

1. No real kernel execution (cuLaunchKernel via CudaStub mock)
2. ~79 symbols exported but ~40 are functional placeholders
3. cuMemcpyDtoD returns CUDA_ERROR_NOT_SUPPORTED
4. Async stream-ops partial (graphs NOT_IMPLEMENTED)
5. Single-device only

### Phase 3 Status (Not Started)

Phase 3 (API extension: Stream, Event, Memory pool, YAML kernel registry) is
**not started**. Future work. Refer to design doc Phase 3 section for scope.

### Plan Reference

`docs/superpowers/plans/2026-07-01-umd-phase2-ld-preload.md` — detailed C.1-C.9 tasks.

## Phase 3: API Extension (Deferred detail)

### Priority matrix

| Pri | Category | Phase |
|-----|----------|-------|
| P0 | Stream API (cuStreamCreate/Synchronize, async copy) | 3.1 (1-2 w) |
| P0 | True multi-stream parallel launch | 3.1 |
| P1 | Event sync (cuEventRecord/Wait/Elapsed) | 3.2 (1 w) |
| P1 | Memory pool (cuMemset*, cuMemAllocHost) | 3.2 |
| P2 | YAML-based kernel registry (vs manual register) | 3.3 (2-3 w) |
| P2 | cuDeviceGetAttribute | 3.3 |
| P3 | Real ELF/.nv.info parsing | **backlog** (4-6 w, high risk) |
| P3 | Multi-device (CUDA_VISIBLE_DEVICES) | **backlog** |

### Cubin parsing decision

**Recommended**: YAML-based kernel registry in Phase 3.3; ELF parsing in Phase D-3 (tadr-205 deferred).

| Strategy | Coverage | Complexity | Phase |
|----------|----------|-----------|-------|
| Manual register (Phase 1) | "I know all kernels" | Trivial | 1 |
| `CUDA_KERNEL_REGISTRY` env var (Phase 2) | "I list them at startup" | Trivial | 2 |
| YAML config file (Phase 3.3) | "I deploy with config" | Low | 3.3 |
| ELF + .nv.info parsing (D-3) | "I parse any cubin" | **High** | **deferred** |

## Cross-repo Dependencies

| Phase | UsrLinuxEmu change needed | Blocking? |
|-------|---------------------------|-----------|
| 0 | None | No |
| 1 | None | No |
| 2 | None | No (shim uses GpuDriverClient + plugin) |
| 3.1 | None | No |
| 3.2 | None | No |
| 3.3 | Possible cuDeviceGetAttribute ioctl extension | Minor |
| D-1 (tadr-205) | doorbell mmap exposure (ADR-024) | 🔴 Major |
| D-3 (tadr-205) | BasicGpuSimulator kernel execution | 🔴 Major |

**Phase 0-3.2 zero cross-repo dependency** — all work stays in TaskRunner.

## Trigger gate (when does Phase 1 start?)

| Prerequisite | Source | Status |
|--------------|--------|--------|
| Phase 0 complete | internal | 🟡 pending kickoff |
| Explicit PoC requirement identified (POA-1 + POA-2) | tadr-205:43 | ✅ **RESOLVED 2026-06-30** (Oracle recommendation) |
| test-fixture scope stable in CI | CI | 🟢 34/34 ✅ |
| IGpuDriver 31 methods verified | H-3.5 / H-3 follow-up | ✅ complete |

**Critical**: tadr-205 lists "explicit PoC requirement" as gate. Without external pull (real customer, downstream consumer, benchmark target), Phase 1 has no business justification. PoC requirement must be specified before kicking Phase 1.

## Alternatives Considered

### Alt-1: Static-link libcuda_taskrunner.a

| Pros | Cons |
|------|------|
| No LD_PRELOAD env, embedded binary | Users must relink, breaks drop-in deployment |

Rejected: Incompatible with "unmodified CUDA programs run on TaskRunner" goal.

### Alt-2: AddressSanitizer-style LD_AUDIT

| Pros | Cons |
|------|------|
| More control than LD_PRELOAD | API complexity, debugging difficulty |

Rejected: LD_PRELOAD is NVIDIA UMD's own mechanism; no upside to deviate.

### Alt-3: Skip Phase 1, go directly to Phase 2

| Pros | Cons |
|------|------|
| Faster to vectorAdd PoC | Untested CudaRuntimeApi is weak link; shim quality depends on uncached Phase 1 layer |

Rejected: Phase 1 + Phase 2 combined has 4-6 weeks total, vs Phase 2 alone ~3 weeks but with much higher risk.

## Resolved PoC Motivation (POA-1 + POA-2, Added 2026-06-30)

Per Oracle architectural review (2026-06-30), Phase 1's PoC motivation is dual:

**POA-1 (external pull): UsrLinuxEmu Stage 1.4 KFD Consumer**
- When Stage 1.4 starts ("compile real KFD + 5 core ioctls"), it needs a Consumer of `GPU_IOCTL_*` commands beyond the raw CLI tool
- Phase 1's `CudaRuntimeApi` provides the API consumer (high-level Runtime API semantics) for KFD integration testing
- Aligned with UsrLinuxEmu roadmap `usrlinuxemu-gpu-driver-design-2026-06-24.md`

**POA-2 (internal pull): CI Regression Test Baseline**
- Every `gpu_ioctl.h` / `IGpuDriver` interface change needs to be validated by a real consumer
- Phase 1's 8 test cases are the "golden consumer" that exercises the full call chain CudaRuntimeApi → CudaScheduler → IGpuDriver
- Phase 1.5 stretch: replace `CudaStub` with `GpuDriverClient` backend and verify 8/8 still pass

**Why not "wait for external customer":**
- `gap-analysis.md` explicitly warns: investment vs ROI is poor for 3-6 month PoC
- `usrlinuxemu-gpu-driver-design-2026-06-24.md` §路线图: 6-12 月大概率走 Stage 1.4 集成验证；小概率外部 pull

**Falsifiable Phase 1 success criteria:**

| # | Goal | Metric | Verification |
|---|------|--------|--------------|
| 1 | 8 tests pass in CudaStub mode | 8/8 doctest PASS | Local `cd build && ./test_cuda_runtime_api` |
| 2 | 0 dynamic_cast leak in CudaScheduler | grep -c `dynamic_cast<CudaStub\*>` = 0 | `grep -rn "dynamic_cast<CudaStub\*>" src/test_fixture/` |
| 3 | ≥5 IGpuDriver methods exercised | Coverage report ≥5 | manual review or test logging |
| 4 | 3 CLI commands functional | `cuda_runtime_alloc/memcpy/launch` respond OK | manual + script |
| 5 | `getScheduler()` accessor usable | Phase 2 `cuInit` code compiles with `TASKRUNNER_BUILD_MODE=umd-evolution` | cmake build |
| 6 | CudaStub→GpuDriverClient swap preserves 8/8 (stretch) | Phase 1.5 future work | separate task post-Phase 1 |

## Risks

| Risk | Probability | Impact | Mitigation |
|------|------------|--------|------------|
| GpuDriverClient `-ENOSYS` in CudaScheduler | 🟡 M | Phase 1 partial | Track as P2 follow-up; Phase 1 only verifies via CudaStub |
| Real libcuda.so symbols change between CUDA versions | 🟢 L | Phase 2 link error | Stub table re-generated from NVIDIA header |
| UsrLinuxEmu fence_id ABI change | 🟢 L | Phase 1/2 breakage | Lock submodule ref; feature branches |
| vectorAdd cubin hardcode differences | 🟡 M | Phase 2 E2E | `CUDA_KERNEL_REGISTRY` env var pre-registers name→index, skips real ELF |
| Real libcudart.so queries non-stubbed cu* | 🟡 M | Phase 2 link fail | Comprehensive stub table (~200 symbols) required |

## Open Questions for User

| ID | Question | Decision deadline |
|----|----------|-------------------|
| Q1 | Phase 3.3: YAML (recommended) vs ELF parsing? | Before Phase 3 kickoff |
| Q2 | Phase 3 scope: All P0+P1, or also P2? | Before Phase 3 kickoff |
| Q3 | Vulkan extension points: keep or remove? | Before doc commit |
| Q4 ✅ RESOLVED (2026-06-30) | Phase 1 success criteria = POA-1 + POA-2. See §Resolved PoC Motivation below. | RESOLVED 2026-06-30 |
| Q5 | Spec author (you) vs implementation team assignment? | Implementation kickoff |

## Phase 1 Implementation Status (Updated 2026-07-01)

Phase 1 deliverables are now complete on `main` branch:

| Component | Status | Commit |
|-----------|--------|--------|
| `CudaRuntimeApi` class (header) | ✅ Implemented | `020814c` |
| `CudaRuntimeApi` implementation | ✅ Implemented | `cb07353` |
| `TaskRunner::getScheduler()` accessor | ✅ Implemented | `6f7818d` |
| 8 real test cases | ✅ Pass | `8bc847a` |
| 4 CLI commands | ✅ Implemented | `4314dae` |

**Test status (39 cases, 0 failures)**:
- test_cuda_runtime_api: 8/8
- test_cuda_scheduler: 8/8
- test_gpu_architecture: 11/11
- test_gpu_phase2: 12/12

**Phase 2 readiness**: All Phase 2 prerequisites met. `getScheduler()` available, `CudaRuntimeApi` class ready for LD_PRELOAD shim wrapping.

## References

- `tadr-201-unified-scheduler.md` (SUPERSEDED) — original v0.1 UnifiedScheduler decision
- `tadr-202-layered-design.md` (SUPERSEDED) — CommandTranslator decision (not implemented)
- `tadr-203-sync-unified.md` (SUPERSEDED) — SyncSource decision (substituted by fence_id)
- `tadr-204-umd-evolution-scope-clarification.md` — scope rules
- `tadr-205-umd-evolution-poc-roadmap.md` — older roadmap (superseded by this spec)
- `tadr-107-shared-infrastructure-boundary.md` — shared scope rules
- `tadr-108-build-mode-selection.md` — CMake modes
- `tadr-109-igpu-driver-uniform-scheduling.md` — IGpuDriver 31 methods
- `gap-analysis.md` — vs ROCm/CUDA gap (decision authority)
- `research/external-amd-rocm-umd-2026-06-24.md` — AMD UMD reference
- `research/external-nvidia-cuda-umd-2026-06-24.md` — NVIDIA UMD reference
- `include/shared/igpu_driver.hpp` (TaskRunner side) — 31-method interface canonical source
- `UsrLinuxEmu/plugins/gpu_driver/shared/gpu_ioctl.h` — ioctl canonical
- `UsrLinuxEmu ADR-024` — doorbell mmap decision (forward reference for D-1)
- `UsrLinuxEmu ADR-035` — cross-repo governance policy
- `UsrLinuxEmu ADR-036` — 3-way architectural separation

---

**Status**: ACCEPTED (2026-06-30).
**Q4 (PoC motivation) RESOLVED**: 2026-06-30 (POA-1 + POA-2 dual motivation; see §Resolved PoC Motivation).
**Implementation phase**: Ready for Sub-plan B (Phase 1) execution.
**Cross-repo pre-req (Out-of-scope for TaskRunner-end work)**: UsrLinuxEmu submodule pointer bump from `a75f779` → current HEAD (per ADR-035 §Rule 5.1).
