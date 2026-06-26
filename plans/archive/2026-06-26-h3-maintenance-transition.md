# H-3 Maintenance Transition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Archive 3 completed H-3 openspec changes, update TaskRunner cross-repo docs to v2.2 with real commit hashes, and run full build/test verification across both repos.

**Architecture:** 4-phase execution: Phase A (UsrLinuxEmu: archive 3 openspec changes) → Phase B (TaskRunner: update sync-plan.md + cross-repo-h7-template.md) → Phase C (full build/test both repos) → Phase D (UsrLinuxEmu submodule bump + ADR-034 optional detail). Phase A/B are parallelizable across repos; Phase C/D are sequential after A/B.

**Tech Stack:** `openspec archive` CLI, git, CMake, doctest, Catch2

**Prerequisites:**
- H-3.6 (Issue #3: attached_queues weak validation) ✅ resolved — UsrLinuxEmu commit `bf8192f` + `09ae1b0`
- H-3.7 (Issue #2: ioctl path bypass) ✅ resolved — UsrLinuxEmu commit `392a496`
- H-3.8 (Issue #1: stream_id u32→u64) ✅ resolved — UsrLinuxEmu commit `02ae421`
- TaskRunner TADR-105 all 3 issues marked Accepted ✅ (commits `f3f52d8`, `feaa0f2`, `5ee250a`)
- UsrLinuxEmu ADR-034 ¶Issue #3 + #2 + #1 marked Accepted ✅ (commits `522e671` + `d549208` + `bcd00cf` / `15f9ac6`)

---

## File Structure

### Files to Create
- (none — change is pure cleanup/archive/doc-update)

### Files to Modify (TaskRunner end)
- `docs/07-integration/cross-repo-h7-template.md` — fill 5 TBDs in history table + update @维护 section
- `plans/sync-plan.md` — bump to v2.2, add H-3.5/3.6/3.7/3.8 completion, remove todo items

### Files to Move (UsrLinuxEmu end)
- `openspec/changes/2026-06-26-h3-6-issue-3-coordination/` → `openspec/changes/archive/`
- `openspec/changes/2026-06-26-h3-7-issue-2-coordination/` → `openspec/changes/archive/`
- `openspec/changes/2026-06-26-h3-8-issue-1-coordination/` → `openspec/changes/archive/`

### Existing Commits Reference (for filling TBDs)

| H-# | Issue | UsrLinuxEmu PR/commit | TaskRunner bump | Submodule bump commit |
|-----|-------|----------------------|-----------------|-----------------------|
| H-3.6 | #3 attached_queues validation | `bf8192f` + `09ae1b0` (pushbuffer validation feat + test) | `f3f52d8` | `522e671` (UsrLinuxEmu) |
| H-3.7 | #2 ioctl path via GpuQueueEmu | `392a496` (refactor: route pushbuffer through GpuQueueEmu) | `73390ae` | `d549208` (UsrLinuxEmu) |
| H-3.8 | #1 stream_id u32→u64 ABI | `02ae421` (feat: widen stream_id) | `9e3db2e` | `93b82b7` (UsrLinuxEmu) |

### Sync-plan v2.2 changes
- Bump version to v2.2
- Add §1.2 subsection: H-3.5/3.6/3.7/3.8 architecture status
- Remove §5.3 H-3.5 + H-7 ADR todo items (both done)
- Update §三 to mention H-3.5/3.6/3.7/3.8 as completed
- Update date to 2026-06-26

---

### Phase A: UsrLinuxEmu — Archive 3 Openspec Changes

**Location:** `/workspace/project/UsrLinuxEmu`

- [ ] **A.1: Archive H-3.6**

    ```bash
    cd /workspace/project/UsrLinuxEmu
    openspec archive 2026-06-26-h3-6-issue-3-coordination -y --skip-specs
    ```

    Verify:
    ```bash
    ls openspec/changes/archive/2026-06-26-h3-6-issue-3-coordination/
    # Expected: proposal.md design.md tasks.md spec.md .openspec.yaml
    ```

- [ ] **A.2: Archive H-3.7**

    ```bash
    cd /workspace/project/UsrLinuxEmu
    openspec archive 2026-06-26-h3-7-issue-2-coordination -y --skip-specs
    ```

    Verify:
    ```bash
    ls openspec/changes/archive/2026-06-26-h3-7-issue-2-coordination/
    # Expected: proposal.md design.md tasks.md spec.md .openspec.yaml
    ```

- [ ] **A.3: Archive H-3.8**

    ```bash
    cd /workspace/project/UsrLinuxEmu
    openspec archive 2026-06-26-h3-8-issue-1-coordination -y --skip-specs
    ```

    Verify:
    ```bash
    ls openspec/changes/archive/2026-06-26-h3-8-issue-1-coordination/
    # Expected: proposal.md design.md tasks.md spec.md .openspec.yaml
    ```

- [ ] **A.4: Commit archive**

    ```bash
    cd /workspace/project/UsrLinuxEmu
    git add openspec/changes/archive/
    git rm -r openspec/changes/2026-06-26-h3-6-issue-3-coordination/
    git rm -r openspec/changes/2026-06-26-h3-7-issue-2-coordination/
    git rm -r openspec/changes/2026-06-26-h3-8-issue-1-coordination/
    git status   # verify clean
    git commit -m "chore(openspec): archive H-3.6/3.7/3.8 coordination (H-7 resolved)"
    git push origin main
    ```

- [ ] **A.5: Verify openspec changes dirs are gone**

    ```bash
    cd /workspace/project/UsrLinuxEmu
    ls openspec/changes/2026-06-26-h3-6-issue-3-coordination/ 2>&1 || echo "✅ archived"
    ls openspec/changes/2026-06-26-h3-7-issue-2-coordination/ 2>&1 || echo "✅ archived"
    ls openspec/changes/2026-06-26-h3-8-issue-1-coordination/ 2>&1 || echo "✅ archived"
    ls openspec/changes/archive/2026-06-26-h3-6-issue-3-coordination/  # should exist
    ls openspec/changes/archive/2026-06-26-h3-7-issue-2-coordination/  # should exist
    ls openspec/changes/archive/2026-06-26-h3-8-issue-1-coordination/  # should exist
    ```

---

### Phase B: TaskRunner — Cross-repo Doc Updates

**Location:** `/workspace/project/UsrLinuxEmu/external/TaskRunner`

- [ ] **B.1: Update `plans/sync-plan.md` to v2.2**

    Changes:

    1. Line 3: `v2.1` → `v2.2`
    2. Line 4: `2026-06-23` → `2026-06-26`
    3. After line 39 (end of §1.2 H-3 architecture block), insert a new subsection:

    ```markdown
    ### 1.3 H-3.5~H-3.8 补丁修复（2026-06-26）
    
    ```
    ┌────────────────────────────────────────────────────────────┐
    │ H-3.5 (2026-06-25): CudaStub guard verification follow-up   │
    │   - 2 dynamic_cast removed (MockGpuDriver + IGpuDriver)     │
    │   - IGpuDriver extended to 31 methods (tadr-109)           │
    │   - TADR-105 agenda tracking                               │
    └────────────────────────────────────────────────────────────┘
    ┌────────────────────────────────────────────────────────────┐
    │ H-3.6 (2026-06-25): ADR-034 Issue #3 → attached_queues    │
    │   - UsrLinuxEmu: bf8192f (pushbuffer VA+Queue validation) │
    │   - UsrLinuxEmu: 09ae1b0 (4 test cases)                   │
    │   - TaskRunner: f3f52d8 (coordination doc)                │
    └────────────────────────────────────────────────────────────┘
    ┌────────────────────────────────────────────────────────────┐
    │ H-3.7 (2026-06-25): ADR-034 Issue #2 → ioctl path bypass  │
    │   - UsrLinuxEmu: 392a496 (route pushbuffer via GpuQueueEmu)│
    │   - TaskRunner: 73390ae (coordination + H-3.7 kickoff)    │
    └────────────────────────────────────────────────────────────┘
    ┌────────────────────────────────────────────────────────────┐
    │ H-3.8 (2026-06-26): ADR-034 Issue #1 → stream_id u32→u64 │
    │   - UsrLinuxEmu: 02ae421 (widen stream_id + compat alias) │
    │   - TaskRunner: 9e3db2e (ABI coordination + test design)  │
    │   - TaskRunner: 5ee250a (tadr-105 §Issue #1 → Accepted)  │
    └────────────────────────────────────────────────────────────┘
    ```
    ```

    4. Update §三 header and add H-3.5/3.6/3.7/3.8 rows in table:

    Line 61: `## 三、Phase 1.5 和 Phase 2 已完成`

    → `## 三、Phase 1.5、Phase 2 及 H-3 补丁已全部完成`

    5. After line 83 (§3.3 cross-repo sync history table), add a new row before the H-4 row:

    ```
    | H-3.5 | h3-5-cudastub-guard-fix | ✅ archived | 2026-06-25 (5ff8c26 commit) |
    ```

    6. Update §5.3 (lines 165–171): Remove "H-3.5" and "H-7 ADR" todo entries from the candidate table, since both are now resolved. Replace with a single resolved row:

    ```diff
    - | **H-3.5** | CudaStub guard verification | 0.5 天 | TADR-006 |
    - | **H-7 ADR** | 3 upstream issues | 1-2 周 | TADR-008 |
    + | ~~H-3.5~~ | CudaStub guard verification | ✅ 完成 (2026-06-25, 5ff8c26) | TADR-006 |
    + | ~~H-7 ADR~~ | 3 upstream issues | ✅ 全部完成 (H-3.6/3.7/3.8) | TADR-008 |
    ```

    7. Line 208 (last updated): Update date to `2026-06-26（H-3 maintenance transition, v2.2 新增 H-3.5~H-3.8 完成详情）`

- [ ] **B.2: Update `docs/07-integration/cross-repo-h7-template.md` — fill history TBDs**

    Replace lines 271–275 (history table) with:

    ```markdown
    | H-# | Issue | UsrLinuxEmu Commit | TaskRunner bump | TADR 状态 | 归档时间 |
    |-----|-------|-------------------|-----------------|-----------|---------|
    | H-3.6 | #3 attached_queues 弱校验 | `bf8192f` | `f3f52d8` | tadr-105 §Issue #3 → Accepted | 2026-06-26 |
    | H-3.7 | #2 ioctl 绕过 | `392a496` | `73390ae` | tadr-105 §Issue #2 → Accepted | 2026-06-26 |
    | H-3.8 | #1 stream_id u32→u64 | `02ae421` | `9e3db2e` | tadr-105 §Issue #1 → Accepted | 2026-06-26 |
    ```

- [ ] **B.3: Update `docs/07-integration/cross-repo-h7-template.md` — @维护 section**

    Line 281: `- **更新时机**: 每完成 1 个 H-7 Issue 修复后，更新"历史 PR 范例"表`

    → `- **更新时机**: 每完成 1 个 H-7 Issue 修复后，更新"历史 PR 范例"表。当前所有 3 个 H-7 Issues 已全部修复归档 (2026-06-26)`

- [ ] **B.4: Commit TaskRunner changes**

    ```bash
    cd /workspace/project/UsrLinuxEmu/external/TaskRunner
    git add plans/sync-plan.md docs/07-integration/cross-repo-h7-template.md
    git status   # verify clean
    git commit -m "docs(maintenance): sync-plan v2.2 + cross-repo template TBD fill (H-3 maintenance transition)"
    git push origin main
    ```

- [ ] **B.5: Verify docs are clean**

    ```bash
    cd /workspace/project/UsrLinuxEmu/external/TaskRunner
    grep -n "TBD" docs/07-integration/cross-repo-h7-template.md | head -5
    # Expected: no TBD matches (or only non-history-table TBDs)
    grep "H-3.5" plans/sync-plan.md
    grep "v2.2" plans/sync-plan.md
    ```

---

### Phase C: Full Build + Test Verification

- [ ] **C.1: TaskRunner test-fixture mode**

    ```bash
    cd /workspace/project/UsrLinuxEmu/external/TaskRunner
    cmake -B build && cmake --build build -j4
    ctest --test-dir build -V
    ```

    Expected:
    - 4 test executables compile (test_cuda_scheduler, test_gpu_architecture, test_gpu_phase2, test_gpu_buffer_validation)
    - test_cuda_scheduler: 8/8 pass
    - test_gpu_architecture: 10/11 pass (1 pre-existing skip)
    - test_gpu_phase2: 12/12 pass
    - test_gpu_buffer_validation: all pass
    - No new compile warnings

- [ ] **C.2: TaskRunner umd-evolution mode**

    ```bash
    cd /workspace/project/UsrLinuxEmu/external/TaskRunner
    cmake -B build -DTASKRUNNER_BUILD_MODE=umd-evolution && cmake --build build -j4
    ctest --test-dir build -V
    ```

    Expected:
    - test_umd_skeleton: 3/3 pass
    - No new compile warnings

- [ ] **C.3: UsrLinuxEmu full build + test**

    ```bash
    cd /workspace/project/UsrLinuxEmu
    cmake -B build && cmake --build build -j4
    ctest --test-dir build -V
    ```

    Expected:
    - test_gpu_plugin: all pass
    - test_gpu_fence_return: all pass
    - test_queue_puller_integration: all pass
    - No new compile warnings

- [ ] **C.4: ABI regression check**

    ```bash
    cd /workspace/project/UsrLinuxEmu/external/TaskRunner
    ctest --test-dir build -V -R phase2
    # Verify: test_gpu_phase2 12/12, especially T4 (create_queue) + T5 (destroy_queue) + T8 (submit_batch) + T9 (wait_fence)
    ```

---

### Phase D: UsrLinuxEmu Submodule Bump + ADR-034 Optional

- [ ] **D.1: Submodule bump**

    ```bash
    cd /workspace/project/UsrLinuxEmu
    git add external/TaskRunner
    git status   # verify modified: external/TaskRunner
    git commit -m "chore(submodule): bump TaskRunner to <TASKRUNNER_HASH> (H-3 maintenance transition)"
    git push origin main
    ```

    Fill `<TASKRUNNER_HASH>` with actual hash from `B.4 git rev-parse HEAD`.

- [ ] **D.2 (Optional): Update ADR-034 completion details**

    If owner has time, add completion detail paragraphs to `docs/00_adr/adr-034-h7-deferred-registry.md` for each of the 3 issues (Item #3/#2/#1), referencing the archive commits.

    ```markdown
    ### 2026-06-26: All 3 H-7 Issues Resolved (H-3.6/3.7/3.8)

    所有 3 个 H-7 注册 issue 已通过 H-3.6/3.7/3.8 修复完成。
    详情见已归档的 openspec changes:
    - openspec/changes/archive/2026-06-26-h3-6-issue-3-coordination/
    - openspec/changes/archive/2026-06-26-h3-7-issue-2-coordination/
    - openspec/changes/archive/2026-06-26-h3-8-issue-1-coordination/
    ```

- [ ] **D.3: Verify end-to-end**

    ```bash
    cd /workspace/project/UsrLinuxEmu
    git log --oneline -6
    # Expected chain:
    # chore(submodule): bump TaskRunner to <hash> (H-3 maintenance transition)
    # chore(openspec): archive H-3.6/3.7/3.8 coordination (H-7 resolved)
    # (earlier H-3.8 commits...)
    cd /workspace/project/UsrLinuxEmu/external/TaskRunner
    git log --oneline -2
    # Expected chain:
    # docs(maintenance): sync-plan v2.2 + cross-repo template TBD fill
    # (earlier commits...)
    ```

---

## Self-Review

### 1. Spec Coverage

| Spec requirement | Task(s) covering it |
|-----------------|---------------------|
| Archive 3 openspec changes (H-3.6/.7/.8) | A.1–A.5 |
| Update `cross-repo-h7-template.md` history TBDs | B.2, B.3 |
| Update `sync-plan.md` to v2.2 | B.1 |
| Full build + test (TaskRunner test-fixture) | C.1 |
| Full build + test (TaskRunner umd-evolution) | C.2 |
| Full build + test (UsrLinuxEmu) | C.3 |
| Submodule bump | D.1 |
| ADR-034 optional detail | D.2 |
| End-to-end verification | D.3 |

### 2. Placeholder scan

- ❌ `D.1` has `<TASKRUNNER_HASH>` — this is intentional (cannot know hash until B.4 is run)
- ✅ All other steps have exact commands and expected output

### 3. Type consistency

- All commit hashes (bf8192f, 09ae1b0, 392a496, 02ae421, f3f52d8, 73390ae, 9e3db2e, 5ee250a) cross-checked against git log output
- Version numbering consistent (v2.1 → v2.2, 2026-06-23 → 2026-06-26)
- All file paths verified via `read` tool

---

## Execution Handoff

Plan complete and saved to `plans/superpowers/2026-06-26-h3-maintenance-transition.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints

**Which approach?**