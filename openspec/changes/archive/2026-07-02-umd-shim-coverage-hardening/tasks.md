# Tasks: umd-shim-coverage-hardening

> **状态**: ⚠️ PROPOSED（2026-07-02）
> **依赖**: Phase 1.5 Stretch (`openspec/changes/archive/2026-07-02-taskrunner-umd-backend-enable/`) ✅ DONE
> **约束**: 79 cu\* 符号不变 + 76+ 现有测试全过 + 无 UsrLinuxEmu 代码改动

---

## 前置条件：基线验证 ✅

- [ ] **0.1** 确认 Phase 1.5 Stretch 已 commit + push：
  ```bash
  cd /workspace/project/UsrLinuxEmu/external/TaskRunner
  git log --oneline -3
  # 输出: 包含 "fix(scheduler): remove dynamic_cast<CudaStub*> hardcoding"
  ```
- [ ] **0.2** 确认 76/76 测试基线全 PASS：
  ```bash
  cd build && cmake .. -DTASKRUNNER_BUILD_MODE=umd-evolution && make -j4
  for t in test_cuda_scheduler test_gpu_architecture test_gpu_phase2 \
            test_cuda_runtime_api test_cuda_shim; do
    ./$t 2>&1 | tail -1
  done
  # 预期: 全部 "Status: SUCCESS!"
  ```
- [ ] **0.3** 确认 79 cu\* 符号不变：
  ```bash
  nm -D --defined-only build/libcuda_taskrunner.so | grep -c " cu[A-Z]"
  # 预期: 79
  ```
- [ ] **0.4** 确认 docs-audit.sh 54/54 PASS：
  ```bash
  ./tools/docs-audit.sh 2>&1 | tail -5
  # 预期: PASS
  ```

---

## Sub-plan A: H-3 Follow-up Fixes (F1-F4) ⏱ ~20 min

> **单 commit**: `docs(h3): cleanup post-activation regressions`

### A.1 [MEDIUM] F1: README.md ACTIVE/DRAFT 不一致

- [ ] Read `openspec/changes/h3-phase2-management/README.md` lines 1-100
- [ ] Replace lines 54-65 (file listing) — DRAFT → ACTIVE markers
- [ ] Delete lines 67-73 (激活流程 5-step section)
- [ ] Replace lines 75-82 with new 历史与交叉引用 section
- [ ] Verify: `grep -n "DRAFT\|plans/2026-06-19-h3\|待建\|激活流程" openspec/changes/h3-phase2-management/README.md` returns only line 1

### A.2 [MEDIUM] F2: tasks.md 测试计数 10/10 → 12/12

- [ ] Read `openspec/changes/h3-phase2-management/tasks.md`
- [ ] Find all "10 tests" / "10/10" references
- [ ] Replace with "12 tests" / "12/12"
- [ ] Verify: `grep -n "10 tests\|10/10" openspec/changes/h3-phase2-management/tasks.md` returns 0 matches

### A.3 [LOW] F3: design.md vs spec.md 日志冲突

- [ ] Read `openspec/changes/h3-phase2-management/design.md` — locate log section
- [ ] Read `openspec/changes/h3-phase2-management/specs/gpu-phase2-management/spec.md` — locate R3
- [ ] Reconcile wording (prefer spec.md as authoritative)
- [ ] Verify: `diff <(grep -A3 "log" design.md) <(grep -A3 "log" spec.md)` shows no semantic conflict

### A.4 [MINOR] F4: design.md:277 缺日期前缀

- [ ] Read line 277 of `openspec/changes/h3-phase2-management/design.md`
- [ ] Add `2026-06-22` prefix per N4 fix style
- [ ] Verify: `grep -n "2026-XX" openspec/changes/h3-phase2-management/design.md` returns 0 matches

### A.5 Commit Sub-plan A

- [ ] Single commit:
  ```
  docs(h3): cleanup post-activation regressions (F1-F4)

  Resolves 4 minor follow-up fixes from UsrLinuxEmu/docs/07-integration/h3-activation-followup.md:
  - F1 [MEDIUM]: README.md ACTIVE/DRAFT inconsistency
  - F2 [MEDIUM]: tasks.md test count 10/10 → 12/12
  - F3 [LOW]: design.md vs spec.md log conflict
  - F4 [MINOR]: design.md:277 missing date prefix

  All 4 items are <5 line modifications per F1-F4 spec.
  ```

---

## Sub-plan B: cuDeviceGetAttribute Expansion ⏱ ~1 day

> **合并 commit**: `feat(shim): Phase 1.6 cuDeviceGetAttribute expansion (B.1-B.4)`

### B.1 Audit existing cuDeviceGetAttribute

- [ ] Read `src/umd/libcuda_shim/cu_query.cpp` — locate cuDeviceGetAttribute
- [ ] Read `include/cuda.h` — list all CUdeviceAttribute values (~80)
- [ ] Categorize: implemented (6), trivially-addable (30+), backend-dependent (deferred)

### B.2 Add backend-independent attribute cases

- [ ] Add switch cases for: TOTAL_CONSTANT_MEMORY, MAX_SHARED_MEMORY_PER_BLOCK, MEM_PITCH, REGS_PER_BLOCK, MAX_BLOCK_DIM_X/Y/Z, MAX_GRID_DIM_X/Y/Z, MAX_THREADS_PER_BLOCK, MAX_THREADS_PER_MULTIPROCESSOR, COMPUTE_MODE, CONCURRENT_KERNELS
- [ ] Unmapped attributes return `CUDA_ERROR_INVALID_VALUE`
- [ ] Build: `cd build && make -j4` — clean compile

### B.3 Implement cuDeviceComputeCapability standalone

- [ ] Move logic from cuDeviceGetAttribute branch into dedicated `cuDeviceComputeCapability`
- [ ] Update cu_stub_table.inc: STUB → REAL_IMPL in cu_query.cpp
- [ ] Regenerate via `tools/generate_cu_stubs.py`

### B.4 Add 4 cuDeviceGetAttribute test cases

- [ ] Test: cuDeviceGetAttribute(MAX_BLOCK_DIM_X) ≥ 1024
- [ ] Test: cuDeviceGetAttribute(MAX_GRID_DIM_X) ≥ 65535
- [ ] Test: cuDeviceGetAttribute(COMPUTE_CAPABILITY_MAJOR) ≥ 2
- [ ] Test: cuDeviceComputeCapability returns major/minor ≥ 2.0
- [ ] Build: `cd build && make test_cuda_shim -j4`
- [ ] Run: `./build/test_cuda_shim 2>&1 | tail -5` — expect 41+ tests PASS (was 37)

---

## Sub-plan C: Context Config & Error Helpers ⏱ ~1.5 days

> **合并 commit**: `feat(shim): Phase 1.6 context config + error helper impls (C.1-C.5)`

### C.1 Implement cuCtxGetCacheConfig / cuCtxSetCacheConfig

- [ ] Read `src/umd/libcuda_shim/cu_ctx.cpp` — find context state struct
- [ ] Add `CUfunc_cache` field (default `CU_FUNC_CACHE_PREFER_NONE`)
- [ ] Implement Get/Set functions
- [ ] Update cu_stub_table.inc: STUB → REAL_IMPL

### C.2 Implement cuCtxGetSharedMemConfig / cuCtxSetSharedMemConfig

- [ ] Add `CUsharedconfig` field to context state (default `CU_SHARED_MEM_CONFIG_DEFAULT`)
- [ ] Implement Get/Set functions
- [ ] Update cu_stub_table.inc

### C.3 Implement cuCtxGetLimit / cuCtxSetLimit

- [ ] Add `std::map<CUlimit, size_t>` to context state
- [ ] Initialize with defaults (STACK_SIZE=1024, PRINTF_FIFO_SIZE=1024, MALLOC_HEAP_SIZE=8MB, etc.)
- [ ] Implement Get/Set functions (no actual enforcement)
- [ ] Update cu_stub_table.inc

### C.4 Implement cuGetErrorName / cuGetErrorString

- [ ] Add static const lookup table: `CUresult → const char*` for all 80+ error codes
- [ ] Add separate message table for cuGetErrorString
- [ ] Unknown errors return `CUDA_ERROR_INVALID_VALUE`
- [ ] Update cu_stub_table.inc

### C.5 Add 5 test cases for Sub-plan C

- [ ] Test: cuCtxGetCacheConfig after init returns `CU_FUNC_CACHE_PREFER_NONE`
- [ ] Test: cuCtxSetCacheConfig(PREFER_L1) then Get returns same
- [ ] Test: cuCtxGetLimit(STACK_SIZE) returns non-zero
- [ ] Test: cuGetErrorName(CUDA_SUCCESS) returns "cudaSuccess"
- [ ] Test: cuGetErrorName(CUDA_ERROR_OUT_OF_MEMORY) returns "cudaErrorOutOfMemory"
- [ ] Run: `./build/test_cuda_shim 2>&1 | tail -5` — expect 46+ tests PASS

---

## Sub-plan D: Memory Info & Pointer/Func Attributes ⏱ ~1.5 days

> **合并 commit**: `feat(shim): Phase 1.6 memory info + pointer/func attributes (D.1-D.4)`

### D.1 Implement cuMemGetInfo

- [ ] Implement `cuMemGetInfo(size_t* free, size_t* total)`:
  - `total` = `TASKRUNNER_GPU_MEM_SIZE` env var (default 8GB)
  - `free` = `total - sum(allocated_bo_sizes)`
- [ ] NULL pointer check returns `CUDA_ERROR_INVALID_VALUE`
- [ ] Update cu_stub_table.inc

### D.2 Implement cuFuncGetAttribute

- [ ] Implement `cuFuncGetAttribute` for 4 attrs:
  - `CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK` → 1024
  - `CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES` → 48 * 1024
  - `CU_FUNC_ATTRIBUTE_CONST_SIZE_BYTES` → 64 * 1024
  - `CU_FUNC_ATTRIBUTE_NUM_REGS` → 32
- [ ] Unknown attr returns `CUDA_ERROR_INVALID_VALUE`
- [ ] Update cu_stub_table.inc

### D.3 Implement cuPointerGetAttribute

- [ ] Implement `cuPointerGetAttribute` for 5 attrs:
  - `CU_POINTER_ATTRIBUTE_CONTEXT` → current ctx
  - `CU_POINTER_ATTRIBUTE_MEMORY_TYPE` → DEVICE if in BO range, else HOST
  - `CU_POINTER_ATTRIBUTE_DEVICE_POINTER` → identity
  - `CU_POINTER_ATTRIBUTE_RANGE_SIZE` → BO size lookup
- [ ] Unknown attr returns `CUDA_ERROR_INVALID_VALUE`
- [ ] Update cu_stub_table.inc

### D.4 Add 4 test cases for Sub-plan D

- [ ] Test: cuMemGetInfo after init returns `total ≥ free`
- [ ] Test: After cuMemAlloc(4096), cuMemGetInfo shows free decreased
- [ ] Test: cuFuncGetAttribute(MAX_THREADS_PER_BLOCK) returns 1024
- [ ] Test: cuPointerGetAttribute(CTX, allocated_ptr) returns valid context
- [ ] Run: `./build/test_cuda_shim 2>&1 | tail -5` — expect 50+ tests PASS

---

## Sub-plan E: Test Coverage Expansion + docs-audit Enhancement ⏱ ~1 day

> **合并 commit**: `feat(shim): Phase 1.6 coverage expansion + docs-audit enhancement + Phase 3 skeleton (E.1-E.5)`
> 
> 本 sub-plan 与 B/C/D 合并为最终 single feat commit（保持 change 边界清晰）

### E.1 Threading tests

- [ ] Test: `cuCtx thread-local isolation` — 2 threads create+destroy ctx, verify no cross-contamination
- [ ] Test: `cuMemAlloc thread safety` — 4 threads each allocate 1KB, all succeed

### E.2 Error path tests

- [ ] Test: cuMemAlloc NULL dptr rejection → `CUDA_ERROR_INVALID_VALUE`
- [ ] Test: cuMemFree invalid ptr rejection → error
- [ ] Test: cuModuleLoad nonexistent file rejection → error
- [ ] Test: cuCtxGetCurrent before cuCtxCreate → `CUDA_ERROR_INVALID_CONTEXT`

### E.3 Add 2 docs-audit checks (G5)

- [ ] Read `tools/docs-audit.sh` Check 9 (lines 320-367)
- [ ] Add check: critical API count ≥41
- [ ] Add check: backend-independent stub count ≤28
- [ ] Verify: `./tools/docs-audit.sh 2>&1 | grep "Phase 1.6"` shows 2 PASS

### E.4 Phase 3 skeleton preparation

- [ ] Create `docs/superpowers/plans/2026-07-02-umd-phase3-skeleton.md`:
  - Copy Phase 3 priority matrix from `phase-3-deferred.md`
  - Add Trigger Conditions section (4 conditions)
  - Add Open Decisions section (Q1-Q5)
  - Mark as DRAFT (no implementation, design reservation only)
- [ ] Add `phase-3-skeleton` entry to `docs/umd-evolution/roadmap/README.md` Current Snapshot table

### E.5 Final commit (Sub-plans A-E)

- [ ] All 89+ tests pass (76 baseline + 13 new)
- [ ] 79 cu\* symbols (unchanged)
- [ ] Stub count ≤28 (was ~38)
- [ ] docs-audit ≥58/58 checks (was 54)
- [ ] Single commit:
  ```
  feat(shim): Phase 1.6 shim coverage hardening (10 stubs → real + 13 tests + F1-F4 + Phase 3 skeleton)
  
  [full commit message per design.md]
  ```

---

## 最终验证 ✅

- [ ] **1. Build**: `cd build && cmake .. -DTASKRUNNER_BUILD_MODE=umd-evolution && make -j4` — clean
- [ ] **2. Tests**: ≥89 cases PASS (76 + 13)
- [ ] **3. Symbol count**: 79 cu\* (unchanged ABI)
- [ ] **4. Stub count**: `grep -c "// STUB" cu_stub_table.inc` ≤ 28
- [ ] **5. Docs audit**: `./tools/docs-audit.sh` PASS (≥58 checks)
- [ ] **6. H-3 fixes**: All 4 items F1-F4 marked `[x]`
- [ ] **7. Phase 3 skeleton**: `docs/superpowers/plans/2026-07-02-umd-phase3-skeleton.md` exists
- [ ] **8. Cross-repo**: UsrLinuxEmu working tree unchanged except submodule pointer

---

## 提交 + 归档 ⏱

- [ ] **6.1** TaskRunner commit (5 total: 1 docs H-3 + 4 feat sub-plans OR 1 docs + 1 combined feat):
  ```bash
  cd /workspace/project/UsrLinuxEmu/external/TaskRunner
  git add .
  git commit -m "feat(shim): Phase 1.6 shim coverage hardening"
  git push origin main
  ```
- [ ] **6.2** UsrLinuxEmu submodule bump:
  ```bash
  cd /workspace/project/UsrLinuxEmu
  git add external/TaskRunner
  git commit -m "chore(submodule): bump TaskRunner to <hash> for Phase 1.6 shim coverage hardening"
  git push origin main
  ```
- [ ] **6.3** 归档本 change (按 ADR-035):
  ```bash
  # 在 TaskRunner 仓内
  mv openspec/changes/2026-07-02-umd-shim-coverage-hardening openspec/changes/archive/
  # 更新 .openspec.yaml: status: PROPOSED → ARCHIVED + 添加 archived: 2026-07-XX
  ```

---

## 回滚预案

- Sub-plan A (docs only): `git revert <commit>` — zero code risk
- Sub-plans B/C/D (shim impls): `git revert <commit>` — restores STUB behavior (cu_* still returns NOT_IMPLEMENTED but tests pass via skip)
- Sub-plan E (docs-audit + skeleton): `git revert <commit>` — removes 2 audit checks + skeleton doc
- No schema migration needed (additive only)
- No ABI break (79 symbols preserved)