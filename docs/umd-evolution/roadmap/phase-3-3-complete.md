---
SCOPE: UMD-EVOLUTION
STATUS: COMPLETE
PHASE: 3.3
COMPLETION_DATE: 2026-07-08
HEAD_COMMIT_AT_COMPLETION: 498265c
TESTS: 318/318 (270 baseline + 23 event timing + 25 texture/surface)
---

# Phase 3.3 COMPLETE — Event Timing + Texture/Surface Frontend

**Goal**: Extend `libcuda_taskrunner.so` with cuEvent* timing precision fixes and
Texture/Surface frontend (cuArray* + cuTexRef*) implementations. Frontend-only
(no UsrLinuxEmu backend dep).

**Status**: ✅ ALL 5 ATOMIC COMMITS (A.1, A.2+A.3+A.4, A.5, B.1+B.2+B.3, B.4) MERGED TO MAIN.

## Deliverables

| Workstream | Description | Commit | Files |
|------------|-------------|--------|-------|
| **A.1** | Refactor `EventTable` → `EventRecord` struct | `7e05caa` | `cu_event.cpp` |
| **A.2+A.3+A.4** | cuEvent* precision fixes (flag validation + recorded_at + strict semantics) | `b744685` | `cu_event.cpp`, `cuda.h` (CU_EVENT_* + CUDA_ERROR_NOT_PERMITTED) |
| **A.5** | 23 event timing test cases | `bb713e5` | `tests/umd/test_event_timing.cpp` + cmake |
| **B.1+B.2+B.3** | cuArray* (3) + cuTexRef* (8) REAL_IMPL | `09db0b2` | `cu_array.cpp` (NEW) + `cu_texref.cpp` (NEW) + `cu_mem.cpp` (removed 2 old stubs) + `cuda.h` types + `generate_cu_stubs.py` + cmake |
| **B.4** | 25 texture/surface test cases | `47f5d70` | `tests/umd/test_texture_surface.cpp` + cmake |
| Merge | Direct merge to main | `498265c` | - |
| **Archive** | openspec archive | `5d95005` | `archive/2026-07-08-phase3-3-event-texture-impl/` |
| **Cross-repo sync** | UsrLinuxEmu submodule bump | `7c274ab` | `external/TaskRunner` |

## Test Results

- **Total tests**: 318/318 pass
  - `test_cuda_shim`: 103/103
  - `test_cu_stream_capture`: 30/30
  - `test_cu_graph`: 25/25 (Phase 3.1) + 5/5 (Phase 4 addition)
  - `test_cu_mem_pool`: 28/28 (Phase 3.2) + 8/8 (Phase 4 addition)
  - `test_cu_graph_real`: 32/32 (Phase 4, new binary)
  - `test_cu_mem_pool_export`: 13/13 (Phase 4, new binary)
  - `test_event_timing`: 23/23 (Phase 3.3a, new binary)
  - `test_texture_surface`: 25/25 (Phase 3.3b, new binary)
  - `test_cuda_scheduler`: 8/8
  - `test_gpu_architecture`: 11/11
  - `test_gpu_phase2`: 12/12
  - `test_cuda_runtime_api`: 8/8
- **Sanitizers verified** (2026-07-08):
  - ASan: 0 leaks (after CudaScheduler destructor fix `8c1d7ba`)
  - UBSan: 0 undefined behavior
  - TSan: 0 data races

## Shim Coverage Update

| Metric | Before Phase 3.3 | After Phase 3.3 | Delta |
|--------|-------------------|------------------|-------|
| REAL_IMPL | 113 | 123 (documented) | +10 |
| STUB | 45 | 37 | -8 |
| cu* exports | 158 | 160 (3 new) | +2 (entries added) |
| Test count | 270 | 318 | +48 |

## 11 STUB → REAL_IMPL Conversions

- `cuArrayCreate` (memory allocation + descriptor)
- `cuArrayGetDescriptor` (return creation-time descriptor)
- `cuArrayDestroy` (erase + mark destroyed)
- `cuTexRefCreate` (handle alloc)
- `cuTexRefDestroy` (erase + mark destroyed)
- `cuTexRefSetArray` (bind CUarray)
- `cuTexRefSetAddress` (bind BO + offset)
- `cuTexRefSetFormat` (set format descriptor)
- `cuTexRefSetFlags` (set flags)
- `cuTexRefGetAddress` (return address)
- `cuTexRefGetArray` (return bound array)

## Bug Fixes Shipped

- **cuEventRecord bug**: Phase 2 PoC was overwriting `created_at` instead of writing
  to a separate `recorded_at` field. Now distinct.
- **cuEventCreateWithFlags bug**: Was discarding `flags` parameter and passing `0`
  (CU_EVENT_DEFAULT). Now passes through actual flags.
- **CudaScheduler leak (found by ASan)**: Destructor was leaking `driver_` when
  CudaScheduler was constructed but never `initialize()`d. Fixed in `8c1d7ba`.

## Phase 3.3 Known Limitations

1. **Texture 3D addressing not supported**: `cuTexRefSetAddress2D` returns `CUDA_ERROR_NOT_IMPLEMENTED`.
2. **Surface references not supported**: `cuSurfRefCreate/Destroy/SetFormat` all return `CUDA_ERROR_NOT_IMPLEMENTED`.
3. **3D arrays not supported**: `cuArray3DCreate` returns `CUDA_ERROR_NOT_IMPLEMENTED`.
4. **Real GPU sampling not supported**: shim only manages texture metadata, no actual GPU texture sampler logic.
5. **No 2D format / mipmap support**: only basic 1D/2D arrays with `CUarray_format` enum.
6. **Single-context textures**: texture references are not bound to specific contexts.

## Cross-Repo Coordination

Per ADR-035 §Rule 5.1 4-step coordination:
- **Step 1** (IGpuDriver 31→46): ✅ Merged in `e6a34eb` (TaskRunner) + TADR-301
- **Step 2** (UsrLinuxEmu sim primitives + 18 IOCTL): ✅ Merged in `138f15a` (PR #20)
- **Step 3** (TaskRunner shim + E2E): ✅ Merged in `02363b8` (PR #7, then superseded by Phase 3.3 at `498265c`)
- **Step 4** (UsrLinuxEmu submodule bump): ✅ Merged in `458299e` (Phase 3.1+3.2) and `7c274ab` (Phase 3.3)

## Phase 3.3 → Phase 4+ Bridge

Phase 4 real-impl-bridge (PR #8, commits `fbcbe44` + `2595f16`) connects 5 shim APIs
(cuGraphLaunch / cuMemPoolAlloc / cuMemPoolAllocAsync / cuMemPoolFreeAsync /
cuMemPoolExportToShareableHandle) to GpuDriverClient IOCTLs. Phase 3.3 cuEvent*
remains shim-side no-op (CudaStub synchronous — fence_id semantics need full
multi-fence tracking for true async, deferred to Phase 5+).

## Files Added (Phase 3.3)

```
src/umd/libcuda_shim/cu_array.cpp     (NEW, 113 lines)
src/umd/libcuda_shim/cu_texref.cpp    (NEW, 158 lines)
tests/umd/test_event_timing.cpp      (NEW, 23 test cases)
tests/umd/test_texture_surface.cpp   (NEW, 25 test cases)
```

## Files Modified (Phase 3.3)

```
src/umd/libcuda_shim/cu_event.cpp    (refactored: flat map → EventRecord)
src/umd/libcuda_shim/cu_mem.cpp      (removed 2 old stubs: cuArrayCreate, cuTexRefCreate)
include/cuda.h                      (CU_EVENT_* flags + CUDA_ERROR_NOT_PERMITTED + CUarray_format + CUDA_ARRAY_DESCRIPTOR + 9 new function declarations)
cmake/UMDEvolution.cmake            (registered 2 new .cpp + 2 new test binaries)
tools/generate_cu_stubs.py          (added 11 APIs to CRITICAL_APIS_IMPL_REQUIRED + CUDA_DRIVER_APIS)
src/umd/libcuda_shim/cu_stub_table.inc  (8 STUB→REAL_IMPL + 3 new entries, commit 21bd415)
plans/sync-plan.md                   (Phase 3.3 status updated, commits 6e8c810)
```

## Refs

- DRAFT plan: `docs/superpowers/plans/2026-07-05-umd-phase3.3-event-texture.md` (now ACCEPTED)
- Openspec change: `openspec/changes/archive/2026-07-08-phase3-3-event-texture-impl/`
- Cross-repo docs: `external/TaskRunner/plans/sync-plan.md` (v2.4.2)
- Architecture update: `docs/umd-evolution/architecture/runtime-layering.md`
