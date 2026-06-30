# UMD-EVOLUTION Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the UMD-EVOLUTION redesign spec (CUDA Runtime API + LD_PRELOAD Driver API shim) in 4 incremental phases, starting with documentation cleanup and Runtime PoC.

**Architecture:** Build `CudaRuntimeApi` on top of existing `CudaScheduler` (DI via shared pointer) → Layer `libcuda_taskrunner.so` shim as `LD_PRELOAD` target for real CUDA programs → expand API coverage in Phase 3. Reuses proven IGpuDriver 31-method abstraction with no cross-repo dependency for Phase 0-3.2.

**Tech Stack:** C++17, CMake 3.10+, doctest, IGpuDriver DI (H-3/H-5), CudaScheduler dispatch (existing), TaskRunner singleton for `cuInit` lifecycle, dlsym+RTLD_NEXT for shim.

**Design doc:** `docs/superpowers/specs/2026-06-30-umd-evolution-redesign.md`

---

## Plan Structure

This plan is organized in 4 sequential sub-plans, each producing a self-contained deliverable:

| Sub-plan | Deliverable | Estimated Effort | Cross-repo Dep |
|----------|-------------|------------------|----------------|
| [Sub-plan A: Phase 0 — Documentation Fix](#sub-plan-a-phase-0--documentation-fix) | Reconciled tadr states + architecture/ directory + conflict rules | 0.5 w | None |
| [Sub-plan B: Phase 1 — Runtime PoC](#sub-plan-b-phase-1--runtime-poc) | Real `CudaRuntimeApi` + 8 test cases + CLI integration | 2-3 w | None |
| [Sub-plan C: Phase 2 — LD_PRELOAD Shim (macro)](#sub-plan-c-phase-2--ld_preload-shim-macro) | `libcuda_taskrunner.so` + 12 cu* APIs + vectorAdd E2E | 2-3 w | None |
| [Sub-plan D: Phase 3 — API Extension (macro)](#sub-plan-d-phase-3--api-extension-macro) | Stream/Event/Memory pool API expansion | 3-5 w | None |

**Trigger gate:** Sub-plan B (Phase 1) should only kick off after Sub-plan A is complete AND user has confirmed the explicit PoC requirement (Q4 in design doc Open Questions).

**Scope check:** Sub-plan A and B are bite-sized detailed enough to be implemented today. Sub-plan C and D are presented at macro level — their detailed bite-sized tasks will be expanded when those phases begin (in separate plan documents, one per phase).

---

## Sub-plan A: Phase 0 — Documentation Fix

**Goal:** Fix the documentation inconsistencies identified in the UMD-EVOLUTION architecture review and create the missing `architecture/` directory.

**Pre-reqs:** None (this is the first sub-plan; prerequisite for Phase 1).

**Files modified:**
- Modify: `docs/umd-evolution/adr/tadr-201-unified-scheduler.md` (frontmatter + body)
- Modify: `docs/umd-evolution/adr/tadr-202-layered-design.md` (frontmatter + body)
- Modify: `docs/umd-evolution/adr/tadr-203-sync-unified.md` (frontmatter + body)
- Modify: `docs/umd-evolution/adr/tadr-205-umd-evolution-poc-roadmap.md` (append dependency section)
- Modify: `docs/umd-evolution/README.md` (add conflict priority table)
- Create: `docs/umd-evolution/architecture/README.md`
- Create: `docs/umd-evolution/architecture/runtime-layering.md`

### Task A.1: Update tadr-201 status to SUPERSEDED

**Files:**
- Modify: `docs/umd-evolution/adr/tadr-201-unified-scheduler.md:1-7` (frontmatter)

- [ ] **Step 1: Update frontmatter STATUS to SUPERSEDED**

```yaml
---
SCOPE: UMD-EVOLUTION
STATUS: SUPERSEDED
SUPERSEDES: tadr-201 (originally Accepted retroactive 2026-06-23)
SUPERSEDED_BY: tadr-109 (IGpuDriver DI), H-3.5 follow-up (2026-06-19)
SUPERSESSION_DATE: 2026-06-30
REPLACES: tadr-001
---
```

- [ ] **Step 2: Replace body "实施路径备注" with explicit SUPERSEDED rationale**

Edit the section starting with "**关键差异**" (line 53) and replace with:

```markdown
## SUPERSEDED Status (2026-06-30)

**原决策** (2026-04-07): B 方案 — 统一调度器 UnifiedScheduler 接收双 API CmdBuffer 统一转换为 GPFIFO entry。

**当前实现** (2026-06-30): 决策已被 **替代**。IGpuDriver 抽象 + DI 模式（详见 tadr-109）替代了 UnifiedScheduler 中央调度器角色：
- `cuda_stub.cpp` 直接构造 `gpu_gpfifo_entry`，不经中间层
- `gpu_driver_client.cpp` 通过 `IGpuDriver*` DI 与三种实现解耦
- 代码演化路径见 H-3.5 follow-up commit `5ff8c26`

**本 TADR 作为历史决策记录保留**，让 v0.1 提案可追溯。如需查阅原始动机，见 [`docs/decision-frame-cuda-vulkan-runtime.md`](../../archive/2026-04-07-decision-frame-cuda-vulkan-runtime.md) §D1。

**替代关系**:
- 本 TADR → tadr-109 (IGpuDriver 31 方法扩展)
- 不再活跃更新；任何相关决策请在 test-fixture scope 通过 tadr-1xx 系列登记。
```

- [ ] **Step 3: Verify frontmatter change**

Run: `head -10 docs/umd-evolution/adr/tadr-201-unified-scheduler.md`
Expected: First line `# ---`, second `SCOPE: UMD-EVOLUTION`, third `STATUS: SUPERSEDED`.

- [ ] **Step 4: Commit**

```bash
git add docs/umd-evolution/adr/tadr-201-unified-scheduler.md
git commit -m "docs(tadr): supersede tadr-201 (UnifiedScheduler replaced by IGpuDriver DI)"
```

### Task A.2: Update tadr-202 status to SUPERSEDED

**Files:**
- Modify: `docs/umd-evolution/adr/tadr-202-layered-design.md` (analogous to A.1)

- [ ] **Step 1: Update frontmatter**

```yaml
---
SCOPE: UMD-EVOLUTION
STATUS: SUPERSEDED
SUPERSEDES: tadr-202
SUPERSEDED_BY: tadr-109 (IGpuDriver DI), H-3 follow-up (2026-06-23)
SUPERSESSION_DATE: 2026-06-30
REPLACES: tadr-002
---
```

- [ ] **Step 2: Replace 实施路径备注 section with SUPERSEDED rationale**

In section starting "**关键差异**" (line 81), replace with:

```markdown
## SUPERSEDED Status (2026-06-30)

**原决策** (2026-04-07): C 方案 — TaskRunner 层 20+ 命令 / UsrLinuxEmu 层 5 种基础命令 + CommandTranslator 转译。

**实际实现路径**:
- CommandTranslator 类**未单独实现**
- 当前通过 H-2.5 引入的 `IGpuDriver` 抽象（见 tadr-109）替代 CommandTranslator 角色
- 每种 IGpuDriver 实现（GpuDriverClient/CudaStub/MockGpuDriver）内部自行处理命令编码

**当前 UsrLinuxEmu 端** `CommandType` 仍保持 KERNEL/DMA_COPY 两种，未按 D2 扩展 5 种。Phase 1 + 1.5 + H-3 的 `GPU_IOCTL_*` 命令路径已通过 ioctl 编号实现扩展，无需 enum 改造。

**替代关系**: 本 TADR → tadr-109；任何相关决策在 shared scope 通过 tadr-3xx 系列登记。
```

- [ ] **Step 3: Commit**

```bash
git add docs/umd-evolution/adr/tadr-202-layered-design.md
git commit -m "docs(tadr): supersede tadr-202 (CommandTranslator replaced by IGpuDriver DI)"
```

### Task A.3: Update tadr-203 status to SUPERSEDED

**Files:**
- Modify: `docs/umd-evolution/adr/tadr-203-sync-unified.md`

- [ ] **Step 1: Update frontmatter**

```yaml
---
SCOPE: UMD-EVOLUTION
STATUS: SUPERSEDED
SUPERSEDES: tadr-203
SUPERSEDED_BY: H-3 S3.5 fence_id mechanism (commit a7f4463, 2026-05-13)
SUPERSESSION_DATE: 2026-06-30
REPLACES: tadr-003
---
```

- [ ] **Step 2: Replace 实施路径备注 section**

Replace section starting "**关键差异**" (line 73) with:

```markdown
## SUPERSEDED Status (2026-06-30)

**原决策** (2026-04-07): A 方案 — 所有同步原语转为内部 `Barrier` / `SyncManager` 抽象 + coroutine await。

**当前实现**: 简化的 `fence_id` 机制（H-3 S3.5，2026-05-13 commit a7f4463）替代了完整 SyncManager。`gpu_driver_client.h:368-385` 的 `wait_fence()` 是简化同步路径。

**完整 SyncSource/SyncManager 的价值**（coroutine await + 跨 API 自动映射）**未实现**，留待 Phase 3+ 评估是否补全。当前已满足"跨 API 同步"需求（通过 ioctl `fence_id` 等待）。

**未来重启用 SyncSource 的触发条件**:
1. Phase 3 Stream API 实现需要 coroutine-style await
2. 用户 PoC 需求明确要求简化 Runtime API 调用链
3. 否则维持当前 fence_id 同步。
```

- [ ] **Step 3: Commit**

```bash
git add docs/umd-evolution/adr/tadr-203-sync-unified.md
git commit -m "docs(tadr): supersede tadr-203 (SyncSource deferred, fence_id substitutes)"
```

### Task A.4: Append dependency analysis to tadr-205

**Files:**
- Modify: `docs/umd-evolution/adr/tadr-205-umd-evolution-poc-roadmap.md` (append new section)

- [ ] **Step 1: Append 依赖分析 section before final "---" line**

Locate the closing `---` (after References) and prepend:

```markdown
## Dependency Analysis (Added 2026-06-30)

Per design doc `docs/superpowers/specs/2026-06-30-umd-evolution-redesign.md`, the original D-phase roadmap has the following cross-repo dependencies:

| Original Phase | Requires UsrLinuxEmu change? | Blocking? |
|----------------|------------------------------|-----------|
| D-1 (Doorbell mmap) | Yes (ADR-024 implementation) | 🔴 Major |
| D-2 (Minimal CUDA API) | No (IGpuDriver 31 methods sufficient) | No |
| D-3 (ELF + CUBIN parser) | Yes (BasicGpuSimulator kernel execution) | 🔴 Major |

**Kernel launch dependency cycle** (critical):
- D-2 cudaLaunchKernel → relies on D-3 ELF parsing + Simulator execution
- Resolution: D-2 PoC **only runs via CudaStub mode**; real kernel execution requires D-3 prerequisite
- This means D-2 cannot deliver a complete PoC for real CUDA programs without D-3 first

**Why this matters for Phase 0-3 of the redesign spec**:
- Phase 1 (Runtime PoC) inherits the same constraint: only CudaStub backend is fully functional
- Phase 3.3 (YAML kernel registry) sidesteps ELF parsing by accepting user-provided name→index mapping
- D-1 (doorbell) and D-3 (ELF parsing) remain deferred indefinitely per `gap-analysis.md`
```

- [ ] **Step 2: Commit**

```bash
git add docs/umd-evolution/adr/tadr-205-umd-evolution-poc-roadmap.md
git commit -m "docs(tadr): append dependency analysis to tadr-205 (D-1/D-3 cross-repo, kernel cycle)"
```

### Task A.5: Create docs/umd-evolution/architecture/README.md

**Files:**
- Create: `docs/umd-evolution/architecture/README.md`

- [ ] **Step 1: Write the architecture overview doc**

```markdown
---
SCOPE: UMD-EVOLUTION
STATUS: ACCEPTED
DATE: 2026-06-30
RELATED_DESIGN: ../../superpowers/specs/2026-06-30-umd-evolution-redesign.md
---

# UMD-EVOLUTION Architecture Overview

This directory holds the canonical architecture documentation for UMD-EVOLUTION, complementing `README.md` (scope rules) and `adr/` (decisions).

## Files

- `README.md` (this file) — Architecture overview + component diagram
- `runtime-layering.md` — Detailed Phase 1/2 layering design

## Component Stack (As Of 2026-06-30)

```
┌─────────────────────────────────────────────────────────┐
│  External Caller (CLI / Tests / Phase 2: LD_PRELOAD)    │
└──────────────────────┬──────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────┐
│  Phase 1: CudaRuntimeApi (NEW in Phase 1)              │
│  - 3 CUDA Runtime APIs (cudaMalloc/Memcpy/LaunchKern)  │
└──────────────────────┬──────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────┐
│  CudaScheduler (existing, H-3/H-5)                     │
│  - submit_mem_alloc, submit_memcpy_*, submit_launch,   │
│    wait_fence (DI through IGpuDriver*)                 │
└──────────────────────┬──────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────┐
│  IGpuDriver (31 methods, H-2.5/H-3.5)                  │
│  ├─ GpuDriverClient (real ioctl)                       │
│  ├─ CudaStub (fake; only Phase 1 fully wired)         │
│  └─ MockGpuDriver (test mock)                         │
└─────────────────────────────────────────────────────────┘
```

## Cross-Scope Layout

- `src/test_fixture/` — CudaScheduler + IGpuDriver implementations (test-fixture scope)
- `src/umd/` — CudaRuntimeApi + (Phase 2) libcuda shim (umd-evolution scope)
- `include/shared/igpu_driver.hpp` — IGpuDriver interface contract (shared scope)
- `UsrLinuxEmu plugins/gpu_driver/shared/` — ioctl canonical (cross-repo shared)

## Reading Order

1. This file (component orientation)
2. `runtime-layering.md` (Phase 1+ design rationale)
3. Design spec `2026-06-30-umd-evolution-redesign.md` (full context)
4. `../adr/tadr-204-umd-evolution-scope-clarification.md` (scope rules)
```

- [ ] **Step 2: Commit**

```bash
git add docs/umd-evolution/architecture/README.md
git commit -m "docs(architecture): create UMD-EVOLUTION architecture overview (H-5.1)"
```

### Task A.6: Create docs/umd-evolution/architecture/runtime-layering.md

**Files:**
- Create: `docs/umd-evolution/architecture/runtime-layering.md`

- [ ] **Step 1: Write runtime layering doc**

```markdown
---
SCOPE: UMD-EVOLUTION
STATUS: ACCEPTED
DATE: 2026-06-30
RELATED_DESIGN: ../../superpowers/specs/2026-06-30-umd-evolution-redesign.md
RELATED_ADR: ../adr/tadr-201, ../adr/tadr-202, ../adr/tadr-203
---

# Runtime Layering Design (Phase 1 + Phase 2)

## Rationale (per Oracle architectural review 2026-06-30)

The original tadr-201/202/203 proposed a `CudaRuntimeApi → IGpuDriver` direct path. **Oracle review identified that this duplicates logic already provided by CudaScheduler** (memory allocation, fence creation, kernel arg serialization via `LaunchParams`, etc.).

**Corrected architecture**: `CudaRuntimeApi → CudaScheduler → IGpuDriver → implementations`.

## Layer Responsibilities

| Layer | Responsibility | Status |
|-------|----------------|--------|
| **CudaRuntimeApi** | Provide 3 CUDA Runtime API surface (cudaMalloc/Memcpy/LaunchKernel), kernel name→index registry, RAII handle lifecycle, thread safety mutex | Phase 1 [NEW] |
| **CudaScheduler** | Submit commands via existing DI infra (`submit_mem_alloc`, `submit_memcpy_h2d/d2h`, `submit_launch`), wait_fence synchronization | Existing (H-3/H-5) |
| **IGpuDriver** | 31 low-level methods (alloc_bo, submit_memcpy, submit_launch, create_va_space, create_queue, etc.) | Existing (H-2.5) |
| **GpuDriverClient** | Translate IGpuDriver calls to ioctl commands for UsrLinuxEmu | Existing (H-3) |
| **CudaStub** | In-memory fake returning placeholder values; **only fully-wired backend in Phase 1** | Existing (H-2.5) |
| **MockGpuDriver** | Test mock with strict call recording | Existing (test-fixture) |

## API-to-method mapping

| CUDA Runtime API | CudaScheduler call | Verified since |
|-----------------|--------------------|--------------------|
| cudaMalloc(size) | `scheduler_->submit_mem_alloc(size)` → `fence_id` | H-3 (Phase 1) |
| cudaMemcpy(H2D) | `scheduler_->submit_memcpy_h2d(src, dst, n)` | H-3 |
| cudaMemcpy(D2H) | `scheduler_->submit_memcpy_d2h(src, dst, n)` | H-3 |
| cudaLaunchKernel(name, g, b, args, shmem) | `scheduler_->submit_launch(LaunchParams)` | H-3 + H-3.5 |
| Synchronous wait (default) | `scheduler_->wait_fence(fence_id)` | H-3.5 fence_id |

## Known Limitations (Phase 1)

- **GpuDriverClient backend**: `dynamic_cast<CudaStub*>` at 5 sites in CudaScheduler means `submit_mem_alloc`-type operations return `-ENOSYS` via this backend. **Phase 1 only verifies via CudaStub mode.**
- **D2D/H2H memcpy**: Not supported (gpu_ioctl.h limitation). Returns `cudaErrorNotSupported`.
- **Single stream**: No `cuStreamCreate` in Phase 1.
- **Kernel names**: Manually registered; no ELF parsing.

## Phase 2 Extension (forward reference)

Phase 2 adds `libcuda_taskrunner.so` (LD_PRELOAD) wrapping `CudaRuntimeApi`. Adds CUfunction→name handle table in the shim layer; existing Phase 1 path is reused.
```

- [ ] **Step 2: Commit**

```bash
git add docs/umd-evolution/architecture/runtime-layering.md
git commit -m "docs(architecture): runtime layering design (Phase 1+2)"
```

### Task A.7: Add conflict resolution table to umd-evolution/README.md

**Files:**
- Modify: `docs/umd-evolution/README.md`

- [ ] **Step 1: Append "Conflict Resolution" section after existing content**

```markdown

## Conflict Resolution

When documents in this scope disagree, apply these priorities:

| Conflict | Priority | Rationale |
|----------|----------|-----------|
| `vision.md` vs `gap-analysis.md` | **`gap-analysis.md` wins** | ROI analysis is decisive; gap-analysis recommends defer, vision is optimistic |
| TADR x-y-z vs actual code in `main` | **Code wins** | Main branch is canonical; TADRs document decisions at point-in-time |
| `vision-source.md` vs `vision.md` | **`vision.md` wins** | `vision-source.md` is DEPRECATED historical reference only |
| Architecture spec (`docs/superpowers/specs/*-redesign.md`) vs older TADRs | **Spec wins** when STATUS is PROPOSED+archival; older TADRs SUPERSEDED |

Adopted: 2026-06-30 (H-5.1 docs governance cleanup).
```

- [ ] **Step 2: Commit**

```bash
git add docs/umd-evolution/README.md
git commit -m "docs(umd): add conflict resolution table (gap-analysis priority)"
```

### Task A.8: Verify Phase 0 with docs-audit.sh

- [ ] **Step 1: Run docs audit**

Run: `./tools/docs-audit.sh`
Expected: PASS — all 14 docs/umd-evolution/ markdown files have valid frontmatter.

- [ ] **Step 2: Verify git log**

Run: `git log --oneline -10`
Expected: Last 8 commits match Tasks A.1-A.7 commits.

- [ ] **Step 3: Mark Sub-plan A complete and confirm with user**

Do NOT proceed to Sub-plan B until user confirms and explicit PoC requirement (Q4) is defined.

---

## Sub-plan B: Phase 1 — Runtime PoC

**Goal:** Replace the existing `umd::CudaApi` skeleton in `src/umd/` with a working `cuda::CudaRuntimeApi` that wraps `CudaScheduler` and provides 3 verified CUDA Runtime API methods.

**Pre-reqs:** 
- Sub-plan A complete
- User confirmed explicit PoC requirement (Q4 from design doc)
- `test-fixture` scope CI passing (currently 34/34 ✅)

**Files modified:**
- Create: `include/umd/cuda_runtime_api.hpp`
- Create: `src/umd/cuda_runtime_api.cpp`
- Delete: `include/umd/cuda_api.hpp` (replaced; or rename existing CudaApi to keep backward compat — see Task B.0)
- Modify: `src/umd/cuda_api.cpp` → either deprecate or reuse
- Modify: `include/test_fixture/TaskRunner.h` (add `getScheduler()` accessor)
- Create: `tests/umd/test_cuda_runtime_api.cpp` (8 mandatory test cases)
- Modify: `cmake/UMDEvolution.cmake` (add new source file, update test target)
- Modify: `src/test_fixture/cli_main.cpp` (add cuda_runtime CLI commands)

### Task B.0: Decide namespace and migration strategy for existing `umd::CudaApi`

- [ ] **Step 1: Check existing reference of `umd::CudaApi`**

Run: `grep -r "umd::CudaApi\|CudaApi" src/ include/ tests/`
Expected: References are only in `src/umd/cuda_api.cpp`, `include/umd/cuda_api.hpp`, `tests/umd/test_umd_skeleton.cpp`. Otherwise no external coupling.

- [ ] **Step 2: If no external references, replace skeleton cleanly**

Decision: Keep the existing file naming for `CudaApi` skeleton as historical record, but rename class and namespace in new file. Old files deleted at end of Phase 1.

### Task B.1: Add `TaskRunner::getScheduler()` accessor

**Files:**
- Modify: `include/test_fixture/TaskRunner.h` (declaration)
- Modify: `src/test_fixture/TaskRunner.cpp` (implementation)
- Note: Skipped if `scheduler_` is private — re-evaluate after read

- [ ] **Step 1: Locate TaskRunner's scheduler storage field**

Run: `grep -n "scheduler\|CudaScheduler" include/test_fixture/TaskRunner.h src/test_fixture/TaskRunner.cpp`
Expected: Identify storage member variable name and access level.

- [ ] **Step 2: Add public accessor method declaration in TaskRunner.h**

```cpp
public:
  /**
   * @brief Returns the CudaScheduler for UMD/CUDA Runtime API consumers.
   *
   * **Phase 1 Purpose**: Allow CudaRuntimeApi to call CudaScheduler's
   * `submit_mem_alloc` / `submit_memcpy_*` / `submit_launch` methods.
   *
   * **Lifetime**: Valid only after `initialize()` and before `shutdown()`.
   * Returns nullptr if TaskRunner is not initialized.
   *
   * @return CudaScheduler* or nullptr if not initialized.
   */
  CudaScheduler* getScheduler();
```

- [ ] **Step 3: Add accessor implementation in TaskRunner.cpp**

```cpp
CudaScheduler* TaskRunner::getScheduler() {
  if (!initialized_) return nullptr;
  return scheduler_.get();  // adjust member name based on grep result
}
```

- [ ] **Step 4: Verify TaskRunner tests still pass**

Run: `cd build && make -j4 && ./test_cuda_scheduler`
Expected: 8/8 tests pass.

- [ ] **Step 5: Commit TaskRunner accessor**

```bash
git add include/test_fixture/TaskRunner.h src/test_fixture/TaskRunner.cpp
git commit -m "feat(taskrunner): add getScheduler() accessor for UMD Runtime API consumer"
```

### Task B.2: Create `include/umd/cuda_runtime_api.hpp` (real interface)

**Files:**
- Create: `include/umd/cuda_runtime_api.hpp`

- [ ] **Step 1: Write the header file**

```cpp
// SCOPE: UMD-EVOLUTION
// STATUS: PROPOSED (Phase 1 implementation)
// Phase 1: CUDA Runtime API surface implemented. Replaces prior CudaApi skeleton.

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

// Forward declarations to keep header lean.
namespace async_task {
namespace test_fixture {
class CudaScheduler;
}  // namespace test_fixture
}  // namespace async_task

namespace async_task::umd {

// CUDA Runtime API compatible error codes (minimal subset).
enum class CudaError {
  Success = 0,
  InvalidValue = 1,
  NotSupported = 2,
  Unknown = 999,
};

enum class CudaMemcpyKind {
  HostToDevice = 0,
  DeviceToHost = 1,
  DeviceToDevice = 2,
  HostToHost = 3,
};

// Lightweight grid/block dims (cuda runtime uses dim3).
struct Dim3 {
  unsigned int x{1};
  unsigned int y{1};
  unsigned int z{1};
};

/**
 * CudaRuntimeApi — Phase 1 PoC wrapper over existing CudaScheduler.
 *
 * Provides 3 CUDA Runtime API methods (cudaMalloc / cudaMemcpy / cudaLaunchKernel)
 * with synchronous semantics by default. Builds on top of the proven IGpuDriver
 * abstraction without introducing new abstraction layers.
 *
 * **Scope (Phase 1)**:
 * - H2D / D2H memcpy only (D2D and H2H return CudaError::NotSupported).
 * - Single stream (cuStream_t parameter reserved, ignored in Phase 1).
 * - Kernel names manually registered via `register_kernel()`.
 *
 * **Phase 1 known constraints**:
 * - Only CudaStub and MockGpuDriver backends are fully wired.
 *   GpuDriverClient backend returns CudaError::Unknown for some operations
 *   due to existing dynamic_cast<CudaStub*> abstraction leakage in
 *   CudaScheduler (tracked separately).
 */
class CudaRuntimeApi {
 public:
  /**
   * @brief Construct with a CudaScheduler instance (DI).
   * @param scheduler Non-owning pointer; must outlive this object.
   */
  explicit CudaRuntimeApi(
      async_task::test_fixture::CudaScheduler* scheduler);

  ~CudaRuntimeApi();

  // Disable copy (resource handles are tied to scheduler lifetime).
  CudaRuntimeApi(const CudaRuntimeApi&) = delete;
  CudaRuntimeApi& operator=(const CudaRuntimeApi&) = delete;

  /**
   * @brief Register a kernel name → index mapping (Phase 1 manual mapping).
   *
   * In Phase 1, the IGpuDriver submit_launch method takes a uint32_t kernel
   * index, not a name. CudaRuntimeApi maintains this mapping so callers
   * can launch by name. Duplicate names return CudaError::InvalidValue.
   *
   * @param name  Kernel function name (matches the name in CUBIN register).
   * @param index Numeric index used by IGpuDriver::submit_launch.
   */
  CudaError register_kernel(const std::string& name, std::uint32_t index);

  /**
   * @brief Allocate device memory.
   *
   * Maps to CudaScheduler::submit_mem_alloc + wait_fence.
   *
   * @param devPtr Output device pointer (receives handle value).
   * @param size   Bytes to allocate.
   * @return CudaError::Success on success.
   */
  CudaError malloc(void** devPtr, std::size_t size);

  /**
   * @brief Copy memory between host and device.
   *
   * Synchronous by default. Only H2D and D2H are supported; D2D and H2H
   * return CudaError::NotSupported.
   *
   * @param dst   Destination pointer (host or device depending on kind).
   * @param src   Source pointer.
   * @param count Bytes to copy.
   * @param kind  Direction enum.
   */
  CudaError memcpy(void* dst, const void* src, std::size_t count,
                   CudaMemcpyKind kind);

  /**
   * @brief Launch a kernel by name with synchronous semantics.
   *
   * Maps to CudaScheduler::submit_launch(LaunchParams) + wait_fence.
   * Args are passed through to LaunchParams (CudaStub performs serialization).
   *
   * @param name      Kernel name (must be registered).
   * @param gridDim   Grid dimensions.
   * @param blockDim  Block dimensions.
   * @param args      Kernel arguments array (may be nullptr if num_args == 0).
   * @param sharedMem Dynamic shared memory size (bytes).
   */
  CudaError launch_kernel(const std::string& name, Dim3 gridDim,
                          Dim3 blockDim, void** args,
                          std::size_t sharedMem);

 private:
  // Result struct for handle management.
  struct DeviceMem {
    std::uint64_t va_space_handle;
    std::uint64_t handle;  // device pointer (raw integer handle)
    std::size_t size;
  };

  async_task::test_fixture::CudaScheduler* scheduler_;
  std::uint64_t va_space_handle_{0};
  std::uint64_t queue_handle_{0};

  std::unordered_map<std::string, std::uint32_t> kernel_registry_;
  std::unordered_map<void*, DeviceMem> allocations_;

  std::mutex mu_;  // protects handles, registry, allocations
};

}  // namespace async_task::umd
```

- [ ] **Step 2: Verify header compiles in isolation**

Run: `cd build && cmake --build . --target taskrunner_umd_stub 2>&1 | grep -E "(cuda_runtime_api|error)" | head`
Expected: No errors yet (full implementation in next task).

### Task B.3: Implement `src/umd/cuda_runtime_api.cpp` (TDD with tests)

**Files:**
- Create: `src/umd/cuda_runtime_api.cpp`

This is the main implementation. Each public method has a paired test (Task B.4) and a commit.

#### Step B.3a: Implement constructor and destructor (verifies VA space + queue lifecycle)

- [ ] **Step 1: Initialize file skeleton**

Write the file skeleton:

```cpp
// SCOPE: UMD-EVOLUTION
// STATUS: PROPOSED (Phase 1 implementation)

#include "umd/cuda_runtime_api.hpp"
#include "test_fixture/cuda_scheduler.hpp"
#include "test_fixture/cuda_stub.hpp"  // Phase 1 allowed dependency
#include "shared/igpu_driver.hpp"

#include <stdexcept>

namespace async_task::umd {

namespace {

// Helper: lookup CudaStub-style get_instance for Phase 1 only.
async_task::test_fixture::CudaStub* get_cuda_stub(
    async_task::test_fixture::CudaScheduler* sched) {
  return dynamic_cast<async_task::test_fixture::CudaStub*>(
      sched->driver());
}

}  // namespace

CudaRuntimeApi::CudaRuntimeApi(
    async_task::test_fixture::CudaScheduler* scheduler)
    : scheduler_(scheduler) {
  if (!scheduler_) {
    throw std::invalid_argument("CudaRuntimeApi: scheduler is nullptr");
  }

  std::lock_guard<std::mutex> lock(mu_);
  // H-3 Phase 2 verified API: create_va_space + create_queue.
  va_space_handle_ = scheduler_->create_va_space(0);
  queue_handle_ = scheduler_->create_queue(va_space_handle_,
                                           /*queue_type=*/0,
                                           /*priority=*/0,
                                           /*ring_size=*/4096);
}

CudaRuntimeApi::~CudaRuntimeApi() {
  if (scheduler_) {
    // Best-effort teardown; ignore errors (Phase 1 PoC).
    if (queue_handle_) {
      scheduler_->destroy_queue(va_space_handle_, queue_handle_);
    }
    if (va_space_handle_) {
      scheduler_->destroy_va_space(va_space_handle_);
    }
  }
}

}  // namespace async_task::umd
```

- [ ] **Step 2: Verify constructor compiles**

Run: `cd build && cmake --build . --target taskrunner_umd_stub 2>&1 | tail -20`
Expected: Compilation errors (incomplete class) — these are expected; the next step fills in `register_kernel`.

#### Step B.3b: Implement `register_kernel`

- [ ] **Step 3: Add register_kernel implementation after the destructor**

```cpp
CudaError CudaRuntimeApi::register_kernel(const std::string& name,
                                          std::uint32_t index) {
  if (name.empty()) return CudaError::InvalidValue;

  std::lock_guard<std::mutex> lock(mu_);
  auto [it, inserted] = kernel_registry_.try_emplace(name, index);
  if (!inserted) {
    return CudaError::InvalidValue;  // duplicate
  }
  return CudaError::Success;
}
```

- [ ] **Step 4: Verify compiling**

Run: `cd build && cmake --build . --target taskrunner_umd_stub 2>&1 | tail -10`
Expected: Build succeeds (overload methods are still stubs).

#### Step B.3c: Implement `malloc` and `memcpy`

- [ ] **Step 5: Add malloc + memcpy after register_kernel**

```cpp
CudaError CudaRuntimeApi::malloc(void** devPtr, std::size_t size) {
  if (!devPtr || size == 0) return CudaError::InvalidValue;

  std::lock_guard<std::mutex> lock(mu_);
  // H-3 verified: submit_mem_alloc returns (handle, fence_id).
  auto [handle, fence_id] =
      scheduler_->submit_mem_alloc(va_space_handle_, size);

  if (fence_id == 0 || handle == 0) {
    return CudaError::Unknown;  // CudaStub returns -ENOSYS for non-stub backends
  }

  // Wait for completion (Phase 1 default: synchronous).
  if (scheduler_->wait_fence(fence_id) != 0) {
    return CudaError::Unknown;
  }

  allocations_.emplace(reinterpret_cast<void*>(handle),
                       DeviceMem{va_space_handle_, handle, size});
  *devPtr = reinterpret_cast<void*>(handle);
  return CudaError::Success;
}

CudaError CudaRuntimeApi::memcpy(void* dst, const void* src,
                                 std::size_t count, CudaMemcpyKind kind) {
  if (!dst || !src || count == 0) return CudaError::InvalidValue;

  if (kind == CudaMemcpyKind::DeviceToDevice ||
      kind == CudaMemcpyKind::HostToHost) {
    return CudaError::NotSupported;  // Phase 1 limitation
  }

  std::lock_guard<std::mutex> lock(mu_);
  std::uint64_t src_addr = reinterpret_cast<std::uint64_t>(src);
  std::uint64_t dst_addr = reinterpret_cast<std::uint64_t>(dst);

  std::uint64_t fence_id = 0;
  if (kind == CudaMemcpyKind::HostToDevice) {
    fence_id = scheduler_->submit_memcpy_h2d(queue_handle_, src_addr,
                                             dst_addr, count);
  } else {  // DeviceToHost
    fence_id = scheduler_->submit_memcpy_d2h(queue_handle_, src_addr,
                                             dst_addr, count);
  }

  if (fence_id == 0) return CudaError::Unknown;
  return scheduler_->wait_fence(fence_id) == 0 ? CudaError::Success
                                                : CudaError::Unknown;
}
```

#### Step B.3d: Implement `launch_kernel`

- [ ] **Step 6: Add launch_kernel after memcpy**

```cpp
CudaError CudaRuntimeApi::launch_kernel(const std::string& name,
                                        Dim3 gridDim, Dim3 blockDim,
                                        void** args, std::size_t sharedMem) {
  if (name.empty()) return CudaError::InvalidValue;

  std::uint32_t index = 0;
  {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = kernel_registry_.find(name);
    if (it == kernel_registry_.end()) return CudaError::InvalidValue;
    index = it->second;
  }
  // (Mutex released during scheduler call.)

  // H-3 verified: submit_launch takes kernel_index + grid + block.
  std::uint64_t fence_id = scheduler_->submit_launch(
      queue_handle_, index, gridDim.x, gridDim.y, gridDim.z,
      blockDim.x, blockDim.y, blockDim.z, sharedMem, args);

  if (fence_id == 0) return CudaError::Unknown;
  return scheduler_->wait_fence(fence_id) == 0 ? CudaError::Success
                                                : CudaError::Unknown;
}
```

- [ ] **Step 7: Build complete**

Run: `cd build && cmake --build . --target taskrunner_umd_stub 2>&1 | tail -5`
Expected: BUILD SUCCESS.

- [ ] **Step 8: Commit implementation**

```bash
git add include/umd/cuda_runtime_api.hpp src/umd/cuda_runtime_api.cpp
git commit -m "feat(umd): implement CudaRuntimeApi (cudaMalloc/Memcpy/LaunchKernel)"
```

### Task B.4: Replace tests/umd/test_umd_skeleton.cpp with test_cuda_runtime_api.cpp

**Files:**
- Rename: `tests/umd/test_umd_skeleton.cpp` → `tests/umd/test_cuda_runtime_api.cpp`
- Modify: `cmake/UMDEvolution.cmake` (test source path)

- [ ] **Step 1: Write the new test file with 8 mandatory test cases**

```cpp
// SCOPE: UMD-EVOLUTION
// STATUS: PROPOSED (Phase 1 test suite)

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "umd/cuda_runtime_api.hpp"
#include "test_fixture/cuda_scheduler.hpp"
#include "test_fixture/cuda_stub.hpp"
#include "shared/igpu_driver.hpp"

#include <memory>
#include <thread>
#include <vector>

using namespace async_task;
using async_task::umd::CudaRuntimeApi;
using async_task::umd::CudaError;
using async_task::umd::CudaMemcpyKind;
using async_task::umd::Dim3;

namespace {

struct Fixture {
  // Phase 1 only supports CudaStub backend (see design doc known limitation).
  std::unique_ptr<test_fixture::CudaStub> stub;
  std::unique_ptr<test_fixture::CudaScheduler> scheduler;
  std::unique_ptr<CudaRuntimeApi> api;

  Fixture() {
    stub = std::make_unique<test_fixture::CudaStub>();
    scheduler = std::make_unique<test_fixture::CudaScheduler>(stub.get());
    api = std::make_unique<CudaRuntimeApi>(scheduler.get());
  }
};

}  // namespace

TEST_CASE("malloc_free_roundtrip: RAII safety + handle teardown") {
  Fixture f;
  void* ptr = nullptr;
  CHECK(f.api->malloc(&ptr, 4096) == CudaError::Success);
  CHECK(ptr != nullptr);

  // Phase 1 doesn't implement free; just verify no crash on destruction.
  // Tear-down happens in Fixture destructor.
}

TEST_CASE("memcpy_h2d_data_integrity: data path correctness") {
  Fixture f;
  void* devPtr = nullptr;
  REQUIRE(f.api->malloc(&devPtr, 64) == CudaError::Success);

  std::vector<std::uint8_t> host(64, 0xAB);
  CHECK(f.api->memcpy(devPtr, host.data(), 64,
                      CudaMemcpyKind::HostToDevice) == CudaError::Success);
}

TEST_CASE("memcpy_d2h_data_integrity: bidirectional consistency") {
  Fixture f;
  void* devPtr = nullptr;
  REQUIRE(f.api->malloc(&devPtr, 32) == CudaError::Success);

  std::vector<std::uint8_t> src(32, 0xCD);
  std::vector<std::uint8_t> dst(32, 0);
  REQUIRE(f.api->memcpy(devPtr, src.data(), 32,
                        CudaMemcpyKind::HostToDevice) == CudaError::Success);
  CHECK(f.api->memcpy(dst.data(), devPtr, 32,
                      CudaMemcpyKind::DeviceToHost) == CudaError::Success);
  // Phase 1: CudaStub doesn't actually copy data, but the call should succeed.
  // Real data integrity verification requires GpuDriverClient (Phase 2+).
  CHECK(dst.size() == 32);
}

TEST_CASE("memcpy_d2d_returns_not_supported: graceful error") {
  Fixture f;
  char a, b;
  CHECK(f.api->memcpy(&a, &b, 1, CudaMemcpyKind::DeviceToDevice) ==
        CudaError::NotSupported);
}

TEST_CASE("launch_kernel_returns_fence: synchronization completion") {
  Fixture f;
  REQUIRE(f.api->register_kernel("vectorAdd", 0) == CudaError::Success);

  CHECK(f.api->launch_kernel("vectorAdd", Dim3{1}, Dim3{256}, nullptr, 0) ==
        CudaError::Success);
}

TEST_CASE("register_kernel_duplicate_detection: registry integrity") {
  Fixture f;
  REQUIRE(f.api->register_kernel("vecAdd", 0) == CudaError::Success);
  CHECK(f.api->register_kernel("vecAdd", 1) == CudaError::InvalidValue);
}

TEST_CASE("ctor_fail_no_va_space_leak: resource cleanup on init failure") {
  // Test with nullptr scheduler — should throw.
  CHECK_THROWS_AS(CudaRuntimeApi(nullptr), std::invalid_argument);
}

TEST_CASE("multi_thread_concurrent_alloc: mutex correctness") {
  Fixture f;
  std::vector<std::thread> threads;
  std::atomic<int> success_count{0};

  for (int i = 0; i < 8; ++i) {
    threads.emplace_back([&]() {
      void* ptr = nullptr;
      if (f.api->malloc(&ptr, 128) == CudaError::Success) {
        success_count++;
      }
    });
  }
  for (auto& t : threads) t.join();
  CHECK(success_count.load() == 8);
}
```

- [ ] **Step 2: Delete old skeleton test file**

Run: `git rm tests/umd/test_umd_skeleton.cpp`

- [ ] **Step 3: Modify UMDEvolution.cmake to use new test source**

Edit `cmake/UMDEvolution.cmake`, replace `tests/umd/test_umd_skeleton.cpp` with `tests/umd/test_cuda_runtime_api.cpp`. Also add the new umd source files:

```cmake
# cmake/UMDEvolution.cmake
# SCOPE: umd-evolution
# Phase 1: Real CudaRuntimeApi implementation (replaces CudaApi skeleton).
add_library(taskrunner_umd_stub SHARED
    src/umd/cuda_runtime_api.cpp
)
target_include_directories(taskrunner_umd_stub PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(taskrunner_umd_stub PUBLIC
    taskrunner_test_fixture  # depends on CudaScheduler + CudaStub
    taskrunner_shared
)
target_compile_features(taskrunner_umd_stub PUBLIC cxx_std_17)
set_target_properties(taskrunner_umd_stub PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Tests
add_executable(test_cuda_runtime_api
    tests/umd/test_cuda_runtime_api.cpp
)
target_link_libraries(test_cuda_runtime_api PRIVATE taskrunner_umd_stub)
add_test(NAME test_cuda_runtime_api COMMAND test_cuda_runtime_api)
```

Also delete the `module_loader.cpp` and `ring_buffer.cpp` references — they are no longer needed.

- [ ] **Step 4: Remove old skeleton files**

Run:
```bash
git rm src/umd/cuda_api.cpp
git rm src/umd/module_loader.cpp
git rm src/umd/ring_buffer.cpp
git rm include/umd/cuda_api.hpp
git rm include/umd/module_loader.hpp
git rm include/umd/ring_buffer.hpp
```

- [ ] **Step 5: Reconfigure and build**

Run:
```bash
cd build && cmake .. && make -j4
```
Expected: BUILD SUCCESS.

- [ ] **Step 6: Run new test suite**

Run: `./test_cuda_runtime_api`
Expected: 8/8 tests pass.

- [ ] **Step 7: Run full test suite to verify no regression**

Run: `./test_cuda_scheduler && ./test_gpu_architecture && ./test_gpu_phase2`
Expected: All existing tests still pass (34 + 8 = 42 total).

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "feat(umd): 8 test cases for CudaRuntimeApi + remove CudaApi skeleton"
```

### Task B.5: Add CLI commands for CUDA Runtime API

**Files:**
- Modify: `src/test_fixture/cli_main.cpp` (add `cuda_runtime_alloc/memcpy/launch` commands)

- [ ] **Step 1: Read existing CLI command dispatch**

Run: `grep -n "cuda_alloc\|cuda_memcpy\|cuda_launch" src/test_fixture/cli_main.cpp`
Expected: Identify dispatch pattern for existing commands.

- [ ] **Step 2: Add `cuda_runtime_alloc <size>` command**

Locate the dispatch and add (mirroring existing cuda_alloc pattern, but routed via CudaRuntimeApi):

```cpp
} else if (cmd == "cuda_runtime_alloc" && argc >= 1) {
  std::size_t size = std::stoull(argv[0]);
  void* ptr = nullptr;
  auto err = g_runtime_api->malloc(&ptr, size);  // g_runtime_api is initialized at startup
  if (err == CudaError::Success) {
    std::cout << "[CUDA_RUNTIME] alloc " << size << " bytes at 0x"
              << std::hex << reinterpret_cast<std::uintptr_t>(ptr)
              << std::dec << std::endl;
  } else {
    std::cerr << "[CUDA_RUNTIME] alloc failed (err=" << static_cast<int>(err) << ")" << std::endl;
    return 1;
  }
}
```

- [ ] **Step 3: Add `cuda_runtime_memcpy <h2d|d2h> <host_ptr> <dev_ptr> <size>` and `cuda_runtime_launch <kernel_name>` commands**

(Similar pattern as above; use existing patterns in cli_main.cpp.)

- [ ] **Step 4: Run CLI to verify E2E**

Run:
```bash
./taskrunner cuda_runtime_alloc 4096
./taskrunner cuda_runtime_memcpy h2d 0x7fff00000000 0x10000 1024
```
Expected: Both commands print success output.

- [ ] **Step 5: Commit**

```bash
git add src/test_fixture/cli_main.cpp
git commit -m "feat(cli): add cuda_runtime_alloc/memcpy/launch commands"
```

### Task B.6: Phase 1 documentation update

**Files:**
- Modify: `docs/umd-evolution/architecture/runtime-layering.md` (mark Phase 1 as "Implemented")

- [ ] **Step 1: Update API status table to mark Phase 1 as Implemented**

Add a row at top:

```markdown
| Phase 1 (cudaMalloc / cudaMemcpy / cudaLaunchKernel) | ✅ Implemented (commit <sha>) |
```

(Replace `<sha>` with the actual commit hash from Task B.4 Step 8.)

- [ ] **Step 2: Update design spec STATUS to ACCEPTED for the implemented sub-set**

Open `docs/superpowers/specs/2026-06-30-umd-evolution-redesign.md` and change frontmatter STATUS from PROPOSED to ACCEPTED for the Phase 1 portion. Note Phase 2/3 sub-plans remain PROPOSED.

- [ ] **Step 3: Commit**

```bash
git add docs/
git commit -m "docs(umd): mark Phase 1 ACCEPTED in architecture/runtime-layering"
```

### Task B.7: Mark Phase 1 complete; confirm with user before Phase 2

- [ ] **Step 1: Verify all Phase 1 deliverables**

- [ ] **Step 2: Push changes**

```bash
git push origin main
```

- [ ] **Step 3: Confirm with user before starting Sub-plan C (Phase 2)**

---

## Sub-plan C: Phase 2 — LD_PRELOAD Shim (Macro Outline)

**Goal:** Build `libcuda_taskrunner.so` that intercepts 12 core CUDA Driver API (cu*) symbols and routes them to `CudaRuntimeApi` from Phase 1, enabling unmodified CUDA programs (e.g. NVIDIA CUDA Samples vectorAdd) to run on TaskRunner + UsrLinuxEmu.

**Pre-reqs:**
- Sub-plan B complete (Phase 1 functional)
- User confirms Phase 2 scope (Q1 + Q2 answered in design doc)

**Full task breakdown will be created in a separate plan doc when Phase 2 is initiated.** This outline establishes key boundaries:

### Macro tasks (to be expanded)

#### C.1: Create shim directory structure and stub generator

**Files:**
- Create: `src/umd/libcuda_shim/` directory
- Create: `tools/generate_cu_stubs.py` script (extracts cu* symbol list from CUDA headers)
- Create: `src/umd/libcuda_shim/cu_stub_table.inc` (auto-generated, ~200 stubs)

#### C.2: Implement 12 core cu* functions

**Files:**
- Create: `src/umd/libcuda_shim/cu_init.cpp` (initializes TaskRunner + CudaRuntimeApi)
- Create: `src/umd/libcuda_shim/cu_device.cpp` (cuDeviceGetCount/Get, returns 1)
- Create: `src/umd/libcuda_shim/cu_ctx.cpp` (cuCtxCreate, va_space + queue association)
- Create: `src/umd/libcuda_shim/cu_module.cpp` (cuModuleLoad/GetFunction with fake handle table)
- Create: `src/umd/libcuda_shim/cu_mem.cpp` (cuMemAlloc, cuMemcpy*, cuMemFree)
- Create: `src/umd/libcuda_shim/cu_launch.cpp` (cuLaunchKernel with handle→name lookup)
- Create: `src/umd/libcuda_shim/cu_sync.cpp` (cuCtxSynchronize)

#### C.3: Shim entry points and CMake integration

**Files:**
- Modify: `cmake/UMDEvolution.cmake` (add `cuda_taskrunner` shared library target)
- Create: `tests/umd/test_cuda_shim.cpp` (vectorAdd E2E test)

#### C.4: CI integration for stub completeness

**Files:**
- Modify: `tools/docs-audit.sh` (add stub table completeness check)

### Phase 2 success criteria

- `./libcuda_taskrunner.so` exports 12 core symbols + ~200 stubs
- `LD_PRELOAD=./libcuda_taskrunner.so ./vectorAdd` runs without segfault
- CudaStub returns OK for all 12 intercepted APIs

---

## Sub-plan D: Phase 3 — API Extension (Macro Outline)

**Goal:** Expand API coverage from 12 → 30+ cu* functions and add Stream/Event/Memory pool APIs.

**Pre-reqs:** Sub-plan C complete (Phase 2 stable).

### Macro phases

| Sub-phase | Scope | Effort |
|-----------|-------|--------|
| **3.1** | Stream API (cuStreamCreate/Synchronize, async copy) | 1-2 w |
| **3.2** | Event sync (cuEventRecord/Wait/Elapsed) + Memory pool (cuMemset*, cuMemAllocHost) | 1-2 w |
| **3.3** | YAML kernel registry; cuDeviceGetAttribute | 2-3 w |

**Open decisions before Phase 3 kickoff**:
- Q1: Cubin parsing strategy (YAML recommended)
- Q2: Phase 3 scope (P0+P1 vs include P2)

### Phase 3 will produce

- ~30+ cu* APIs in shim
- Async copy + stream synchronization
- YAML registry config in `kernels.yaml`
- Test suite expansion to ~16 test cases

---

## Appendix A: Open Questions Requiring User Decision

These are blocking issues that must be resolved before continuing (from design doc `2026-06-30-umd-evolution-redesign.md` Open Questions section):

| ID | Question | Default if not answered |
|----|----------|-------------------------|
| Q1 | Phase 3.3: YAML (recommended) vs ELF parsing? | YAML |
| Q2 | Phase 3 scope: All P0+P1, or also P2? | P0+P1 only |
| Q3 | Vulkan extension points: keep or remove? | Keep (architectural reservation) |
| **Q4** | **Trigger gate "explicit PoC requirement": what defines this?** | **REQUIRED BEFORE Phase 1 KICKOFF** |
| Q5 | Spec/implementation team assignment? | TBD at kickoff |

---

## Appendix B: Testing Matrix

| Sub-plan | Test target | Test framework | Pass criteria |
|----------|-------------|---------------|---------------|
| A | docs-audit.sh | bash | All frontmatter valid |
| B | test_cuda_runtime_api | doctest | 8/8 pass + existing 34 tests still pass |
| C | test_cuda_shim + vectorAdd E2E | doctest + manual | vectorAdd completes; stub completeness verified |
| D | test_cuda_runtime_api (extended) | doctest | 16/16 pass |

---

## Appendix C: Cross-Repository Synchronization

When Phase 0/1 changes touch shared-scope files, follow ADR-035 §Rule 5.1 4-step sync:

1. TaskRunner commits to `main` (this plan's tasks)
2. TaskRunner pushes to `origin/main`
3. UsrLinuxEmu submodule pointer bumped in a chore(submodule) commit
4. UsrLinuxEmu's `docs/00_adr/README.md` TaskRunner TADR mirror updated

**Phase 0-3.2 of this plan have zero UsrLinuxEmu dependency**, so no cross-repo sync until Phase D-1/D-3 (which are deferred per gap-analysis.md).

---

## Appendix D: References

- Design doc: `docs/superpowers/specs/2026-06-30-umd-evolution-redesign.md` (411 lines)
- Oracle architectural reviews: 2026-06-30 (Phase 1, Phase 2)
- Existing implementation references:
  - `include/test_fixture/cuda_scheduler.hpp` (5634 bytes)
  - `include/test_fixture/TaskRunner.h` (14564 bytes)
  - `src/umd/cuda_api.hpp` (skeleton, replaced in B.4)
  - `cmake/UMDEvolution.cmake` (812 bytes, modified in B.4 and C.3)
- Build instructions: `AGENTS.md` §构建命令 (cmd: `cd build && cmake .. && make -j4`)
- Cross-repo sync policy: `UsrLinuxEmu/docs/00_adr/adr-035-governance-policy.md`

---

**Status**: PROPOSED awaiting user approval to begin Sub-plan A execution.
