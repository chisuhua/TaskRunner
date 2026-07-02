# Tasks: phase16-shim-extension

> **状态**: ⚠️ PROPOSED（2026-07-02）
> **依赖**: commit d8ca3d3（hotfix）✅ DONE
> **约束**: 79 cu\* 符号不变 + 37+ 现有测试全过 + 无 UsrLinuxEmu 代码改动

---

## 前置条件：基线验证

- [ ] **0.1** 确认 commit d8ca3d3 存在：
  ```bash
  cd /workspace/project/UsrLinuxEmu/external/TaskRunner
  git log --oneline -3
  # 预期: d8ca3d3 fix(shim): register 42 drift APIs ...
  ```
- [ ] **0.2** 确认 76/76 测试基线：
  ```bash
  cd build && cmake .. -DTASKRUNNER_BUILD_MODE=umd-evolution && make -j4
  for t in test_cuda_scheduler test_gpu_architecture test_gpu_phase2 \
            test_cuda_runtime_api test_cuda_shim; do
    ./$t 2>&1 | tail -1
  done
  # 预期: 全部 "Status: SUCCESS!"
  ```
- [ ] **0.3** 确认 hotfix 后 baseline：
  ```bash
  echo "STUB: $(grep -c '^// STUB$' src/umd/libcuda_shim/cu_stub_table.inc)"     # 65
  echo "REAL_IMPL: $(grep -c '^// REAL_IMPL' src/umd/libcuda_shim/cu_stub_table.inc)"  # 79
  echo "Exported: $(nm -D --defined-only build/libcuda_taskrunner.so | grep -c ' cu[A-Z]')"  # 79
  ```

---

## Sub-plan A.2: cuModuleLoad*Data demote ⏱ 30 min

> **单 commit**: `fix(shim): demote cuModuleLoadData/Ex/FatBinary back to STUB`

### A.2.1: 从 CRITICAL_APIS_IMPL_REQUIRED 移除 3 项

- [ ] Read `tools/generate_cu_stubs.py` lines 75-95 (Module section)
- [ ] Remove from dict:
  - `"cuModuleLoadData": "cu_module.cpp",`
  - `"cuModuleLoadDataEx": "cu_module.cpp",`
  - `"cuModuleLoadFatBinary": "cu_module.cpp",`
- [ ] Verify: `python3 -c "import ast; ..."` shows dict now has 76 entries

### A.2.2: 重新生成 cu_stub_table.inc

- [ ] Run: `python3 tools/generate_cu_stubs.py`
- [ ] Verify output: `Total APIs: 144, Real: 76, Stubs: 68`
- [ ] Verify: `grep -c '^// STUB$' src/umd/libcuda_shim/cu_stub_table.inc` = 68
- [ ] Verify: `grep -c '^// REAL_IMPL' src/umd/libcuda_shim/cu_stub_table.inc` = 76

### A.2.3: Commit A.2

- [ ] Single commit:
  ```
  fix(shim): demote cuModuleLoadData/Ex/FatBinary back to STUB
  
  Hotfix d8ca3d3 incorrectly registered these 3 APIs as REAL_IMPL,
  but their .cpp implementations still return CUDA_ERROR_NOT_IMPLEMENTED.
  Demoting back to STUB marker for honest documentation.
  
  - tools/generate_cu_stubs.py: remove 3 entries from CRITICAL_APIS_IMPL_REQUIRED
  - src/umd/libcuda_shim/cu_stub_table.inc: regenerated (STUB 65→68, REAL_IMPL 79→76)
  
  Long-term: real ELF/CUBIN parser implementation deferred to Phase D-3.
  ```

---

## Sub-plan A.1: cuMemGetInfo 真实数据源 ⏱ 2-3 h

> **单 commit**: `fix(shim): cuMemGetInfo pulls from IGpuDriver real device info`

### A.1.1: 调查 GpuDriverClient 接口

- [ ] Read `include/shared/igpu_driver.hpp` — check existing virtual methods
- [ ] Search for `get_device_info` or similar:
  ```bash
  grep -n "get_device_info\|device_info\|gpu_device_info" include/shared/igpu_driver.hpp src/test_fixture/*.cpp
  ```
- [ ] If exists: use it. If not: plan fallback.

### A.1.2: 修改 cu_mem.cpp cuMemGetInfo

- [ ] Read `src/umd/libcuda_shim/cu_mem.cpp` — locate cuMemGetInfo (currently 4 lines)
- [ ] Replace hardcoded values with IGpuDriver call:
  ```cpp
  extern "C" CUresult cuMemGetInfo(size_t* free, size_t* total) {
    if (!free || !total) return CUDA_ERROR_INVALID_VALUE;
    // Try IGpuDriver first
    auto* driver = async_task::umd::shim::get_driver();
    if (driver) {
      gpu_device_info info;
      int ret = driver->get_device_info(0, &info);
      if (ret == 0) {
        *total = info.total_memory;
        *free = info.total_memory - info.used_memory;  // if available
        return CUDA_SUCCESS;
      }
    }
    // Fallback: env var or default
    const char* env = std::getenv("TASKRUNNER_GPU_MEM_SIZE");
    size_t default_size = env ? std::stoull(env) : (8ULL * 1024 * 1024 * 1024);
    *total = default_size;
    *free = default_size;
    return CUDA_SUCCESS;
  }
  ```
- [ ] Build: `cd build && make -j4` — clean compile

### A.1.3: 测试 A.1

- [ ] Run: `./build/test_cuda_shim 2>&1 | tail -5` — expect 37 PASS (no regression)
- [ ] Add new test in test_cuda_shim.cpp:
  ```cpp
  TEST_CASE("cuMemGetInfo returns real device data via GpuDriverClient") {
    size_t free_mem, total_mem;
    CUresult ret = cuMemGetInfo(&free_mem, &total_mem);
    REQUIRE(ret == CUDA_SUCCESS);
    REQUIRE(total_mem > 0);
    REQUIRE(free_mem <= total_mem);
  }
  ```

### A.1.4: Commit A.1

- [ ] Single commit:
  ```
  fix(shim): cuMemGetInfo pulls from IGpuDriver real device info
  
  Previously hardcoded *free = 4GB / *total = 8GB (fake-success).
  Now queries GpuDriverClient::get_device_info() for real hardware data,
  with env var TASKRUNNER_GPU_MEM_SIZE fallback.
  
  - src/umd/libcuda_shim/cu_mem.cpp: cuMemGetInfo uses IGpuDriver
  - tests/umd/test_cuda_shim.cpp: +1 test for real data source
  ```

---

## Sub-plan A.3: 测试覆盖扩展 ⏱ 1-2 d

> **单 commit**: `test(shim): Phase 1.6 coverage expansion — 8 newly-registered APIs (37→47 cases)`

### A.3.1: cuEvent tests (4 cases)

- [ ] Add test: `cuEventCreate returns valid event handle`
- [ ] Add test: `cuEventRecord records event on stream`
- [ ] Add test: `cuEventSynchronize waits for event completion`
- [ ] Add test: `cuEventElapsedTime measures time between events`

### A.3.2: cuStream tests (3 cases)

- [ ] Add test: `cuStreamCreate returns valid stream handle`
- [ ] Add test: `cuStreamSynchronize waits for stream completion`
- [ ] Add test: `cuStreamQuery returns stream status`

### A.3.3: cuCtx tests (3 cases)

- [ ] Add test: `cuCtxGetCacheConfig returns default config`
- [ ] Add test: `cuCtxSetCacheConfig persists and retrieves`
- [ ] Add test: `cuCtxGetLimit returns non-zero stack size`

### A.3.4: cuLaunchCooperativeKernel NOT_SUPPORTED test (1 case)

- [ ] Add test: `cuLaunchCooperativeKernel returns NOT_SUPPORTED (not delegate to cuLaunchKernel)`

### A.3.5: Run all tests + commit

- [ ] Run: `cd build && make test_cuda_shim -j4 && ./build/test_cuda_shim 2>&1 | tail -5`
- [ ] Verify: ≥47 cases PASS (was 37, +10 new)
- [ ] Single commit:
  ```
  test(shim): Phase 1.6 coverage expansion — 8 newly-registered APIs
  
  Add 10 E2E test cases for APIs registered in hotfix d8ca3d3:
  - 4 cuEvent* tests (Create/Record/Synchronize/ElapsedTime)
  - 3 cuStream* tests (Create/Synchronize/Query)
  - 3 cuCtx* tests (CacheConfig/Limit)
  - 1 cuLaunchCooperativeKernel NOT_SUPPORTED test (was silently
    delegating to cuLaunchKernel — review MUST-FIX)
  
  Test count: 37 → 47 (Δ+10)
  ```

---

## Sub-plan H-3: 跨仓 PR 描述 ⏱ 30 min

> **不在 TaskRunner 仓 commit**，生成 PR 描述文件供 UsrLinuxEmu owner 采纳

### H-3.1: 创建 PR 描述文档

- [ ] Create `docs/superpowers/cross-repo-prs/2026-07-02-h3-followup-fixes.md`:
  ```markdown
  # H-3 Follow-up Fixes — PR Description for UsrLinuxEmu
  
  ## Summary
  Resolves 4 minor follow-up fixes from
  UsrLinuxEmu/docs/07-integration/h3-activation-followup.md (sent 2026-06-22).
  
  ## Changes
  ### F1 [MEDIUM]: README.md ACTIVE/DRAFT inconsistency
  - File: openspec/changes/archive/2026-06-22-h3-phase2-management/README.md
  - Fix: Replace DRAFT markers with ACTIVE, delete "激活流程" section
  
  ### F2 [MEDIUM]: tasks.md test count 10/10 → 12/12
  - File: openspec/changes/archive/2026-06-22-h3-phase2-management/tasks.md
  - Fix: Update test count references
  
  ### F3 [LOW]: design.md vs spec.md log conflict
  - Files: openspec/changes/archive/2026-06-22-h3-phase2-management/{design,spec}.md
  - Fix: Reconcile wording (spec.md authoritative)
  
  ### F4 [MINOR]: design.md:277 date prefix
  - File: openspec/changes/archive/2026-06-22-h3-phase2-management/design.md
  - Fix: Add 2026-06-22 prefix
  
  ## Validation
  No code changes; docs only. Existing tests unaffected.
  ```

---

## Sub-plan Phase 3 prep ⏱ 1 h

> **单 commit**: `docs(umd): Phase 3 prep design notes (renamed to avoid phase-3-deferred.md conflict)`

### P3.1: 创建 phase3-prep-design-notes.md

- [ ] Create `docs/superpowers/plans/2026-07-02-phase3-prep-design-notes.md`:
  - Copy Phase 3 priority matrix from `phase-3-deferred.md`
  - Add Trigger Conditions section (4 conditions)
  - Add Open Decisions section (Q1-Q5)
  - Add Effort Estimates section (3.1: 1-2w, 3.2: 2-3w, 3.3: 6-9w)
  - Mark frontmatter as `STATUS: DRAFT`
- [ ] Update `docs/umd-evolution/roadmap/README.md` Current Snapshot table to reference phase3-prep-design-notes.md
- [ ] Single commit:
  ```
  docs(umd): Phase 3 prep design notes

  Create phase3-prep-design-notes.md as DRAFT design reservation,
  preparing for Stage 1.4 trigger. Renamed from "skeleton" to avoid
  naming confusion with existing phase-3-deferred.md.
  ```

---

## 最终验证

- [ ] **1. Build**: `cd build && cmake .. -DTASKRUNNER_BUILD_MODE=umd-evolution && make -j4` — clean
- [ ] **2. All tests**: 
  ```bash
  for t in test_cuda_scheduler test_gpu_architecture test_gpu_phase2 \
            test_cuda_runtime_api test_cuda_shim; do
    ./build/$t 2>&1 | tail -1
  done
  ```
  Expected: ≥87 cases PASS (76 + 10 + 1 = 87)
- [ ] **3. Symbol count**: 79 cu\* (unchanged ABI)
- [ ] **4. Stub count**: `grep -c '^// STUB$' cu_stub_table.inc` = 68 (was 65, +3 from A.2)
- [ ] **5. Real impl count**: `grep -c '^// REAL_IMPL' cu_stub_table.inc` = 76 (was 79, -3 from A.2)
- [ ] **6. Docs audit**: `./tools/docs-audit.sh` — PASS (Critical APIs = 76)
- [ ] **7. H-3 PR description**: `docs/superpowers/cross-repo-prs/2026-07-02-h3-followup-fixes.md` exists
- [ ] **8. Phase 3 prep**: `docs/superpowers/plans/2026-07-02-phase3-prep-design-notes.md` exists

---

## 提交 + 归档

- [ ] Sub-plan A.2 commit (single): cuModuleLoadData demote
- [ ] Sub-plan A.1 commit (single): cuMemGetInfo real data
- [ ] Sub-plan A.3 commit (single): test coverage expansion
- [ ] Sub-plan Phase 3 commit (single): phase3-prep-design-notes
- [ ] H-3 PR description: NOT committed (kept as reference)
- [ ] After all commits: `git push origin main`
- [ ] After validation: archive this change to `openspec/changes/archive/`
- [ ] Update `.openspec.yaml`: `status: PROPOSED → ARCHIVED`

---

## 回滚预案

- A.2 commit: revert restores REAL_IMPL marker (incorrect but documented)
- A.1 commit: revert restores hardcoded values (fake-success but matches Phase 2 baseline)
- A.3 commit: revert removes new tests (no code impact)
- Phase 3 prep commit: revert removes doc (no code impact)
- Single-revert per commit possible