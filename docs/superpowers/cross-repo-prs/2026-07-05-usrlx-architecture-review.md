---
SCOPE: umd-evolution
STATUS: PROPOSED
DATE: 2026-07-05
FROM: UsrLinuxEmu Architecture Team
TO: TaskRunner Phase 3 owner (Sisyphus)
RE: Architecture review response on sim-stream-primitive-support coordination request
TYPE: Cross-repo technical feedback (response to `2026-07-05-phase3-1-stream-mempool-coordination.md`)
---

# Cross-Repo Feedback: UsrLinuxEmu Review on Phase 3.1/3.2 sim Primitive Support

> **Source**: UsrLinuxEmu Architecture Team review of OpenSpec change `openspec/changes/2026-07-05-sim-stream-primitive-support/` (commit pending, status 🔄 PROPOSED).
> **Target**: TaskRunner owner (`external/TaskRunner/docs/superpowers/cross-repo-prs/2026-07-05-phase3-1-stream-mempool-coordination.md`, 444 lines).
> **Review Verdict**: **CONDITIONAL ACCEPT** — 3 blockers + 6 must-fix before implementation; 5 nice-to-have.

## 1. Executive Summary

Thank you for the well-structured coordination request. The 4-step merge sequence (Step 1 → Step 2 → Step 3 → Step 4) is exemplary, and the IGpuDriver 15-method extension plan (per Oracle C1 revision) is exactly the right approach for backward compatibility.

However, our review identified **3 BLOCKER issues** that require TaskRunner team action before we can greenlight Step 2 (the UsrLinuxEmu sim + IOCTL merge):

| # | Issue | Owner | Severity |
|---|-------|-------|----------|
| **B-1** | `GpuQueueEmu` API name mismatch in spec | **TaskRunner** | 🔴 BLOCKER |
| **B-2** | Pool VA range architecture undefined | **TaskRunner + UsrLinuxEmu joint** | 🔴 BLOCKER |
| **B-3** | fence_id lifecycle migration plan incomplete | **TaskRunner shim layer** | 🔴 BLOCKER |
| **F-1** | capture mode range clarification | TaskRunner | 🟡 MUST-FIX |
| **F-2** | attr value blob layout | TaskRunner + UsrLinuxEmu joint | 🟡 MUST-FIX |
| **F-3** | kernargs=0 semantics | TaskRunner | 🟡 MUST-FIX |
| **F-4** | int64_t return convention | TaskRunner + UsrLinuxEmu joint | 🟡 MUST-FIX |
| **N-1** | Underscore vs camelCase naming | TaskRunner | 🟢 NICE |
| **N-2** | Test binary naming convention | TaskRunner | 🟢 NICE |
| **N-3** | Cross-repo PR diff strategy | TaskRunner | 🟢 NICE |
| **N-4** | Documentation sync frequency | TaskRunner + UsrLinuxEmu joint | 🟢 NICE |

This document is mirrored at `openspec/changes/2026-07-05-sim-stream-primitive-support/taskrunner-feedback.md` on the UsrLinuxEmu side.

## 2. BLOCKER Issues (must resolve before Step 2)

### B-1: GpuQueueEmu API name mismatch

**Status**: 🔴 BLOCKER

**Observed**: TaskRunner's Phase 3 spec ([`docs/superpowers/specs/2026-07-02-phase3-stream-capture-design.md`](../../specs/2026-07-02-phase3-stream-capture-design.md)) and the cross-repo PR ([`2026-07-05-phase3-1-stream-mempool-coordination.md`](../../superpowers/cross-repo-prs/2026-07-05-phase3-1-stream-mempool-coordination.md)) both reference `GpuQueueEmu::submit_batch` and `GpuQueueEmu::enqueue` as integration points.

**Actual API** (verified by UsrLinuxEmu Architecture Team, 2026-07-05):

```
$ grep "submit\|enqueue" plugins/gpu_driver/sim/gpu_queue_emu.h
113: int submit(uint64_t gpfifo_addr, uint32_t entry_count);
```

The `GpuQueueEmu` class only exposes a single `submit()` method. There is **no** `submit_batch()` and **no** `enqueue()` method.

**Required action from TaskRunner**:

1. Update `phase3-stream-capture-design.md` and `phase3-mempool-design.md` to reference `GpuQueueEmu::submit(uint64_t, uint32_t)` instead of `submit_batch` / `enqueue`.
2. In the GpuDriverClient forwarding implementation (`src/test_fixture/gpu_driver_client.cpp`), ensure the IOCTL handler signature aligns with `GpuQueueEmu::submit` semantics:
   - `gpu_graph_launch_args` carries `exec_handle` and `stream_id`
   - The driver-side handler (`gpu_drm_driver.cpp` planned for §4.1 of our `tasks.md`) converts these to a gpfifo_addr + entry_count pair before calling `GpuQueueEmu::submit`
3. Confirm in your Step 3 PR that no TaskRunner shim code assumes `submit_batch` or `enqueue` API.

**UsrLinuxEmu commitment**: We will document the exact integration pattern in our `design.md §"与现有 sim 原语的集成"` (see Fix-3 in our internal `fix-steps.md`).

### B-2: Pool VA range architecture undefined

**Status**: 🔴 BLOCKER

**Observed**: TaskRunner's Phase 3.2 mempool spec ([`docs/superpowers/specs/2026-07-02-phase3-mempool-design.md`](../../specs/2026-07-02-phase3-mempool-design.md), 226 lines) describes `cuMemPoolCreate` semantics but does not specify whether the pool VA range is:
- (A) **Reused** from the parent VA Space's existing address range (i.e., pool is a bookkeeping wrapper around `gpu_buddy` allocations), OR
- (B) **Reserved** as a sub-range at pool creation time (i.e., pool carves out a VA window from its `va_space_handle`).

**UsrLinuxEmu decision** (issued 2026-07-05 after Architecture Team review): **Option B (VA sub-range)** is mandatory.

**Rationale**:
- Option A cannot enforce the pool size limit (Spec Scenario 3.6 "alloc exceeding pool size → -ENOSPC" is unimplementable with a global `gpu_buddy_alloc`).
- Option B allows clean isolation between pools and between pools and non-pool VA allocations.

**Required action from TaskRunner**:

1. Update `phase3-mempool-design.md §Pool Allocation` to align with Option B:
   - `cuMemPoolCreate(props, &handle)` causes the driver to reserve a VA sub-range of size `props.maxSize` from the VA Space.
   - `cuMemPoolAlloc(handle, size, &dptr)` returns a VA inside the pool's reserved sub-range.
   - `cuMemPoolTrim(handle, minBytesToKeep)` may release the **upper** portion of the sub-range back to the VA Space.
2. In the GpuDriverClient shim, treat `props.maxSize` as the authoritative pool capacity (do not assume buddy heap global limits).
3. Provide sample test cases in `tests/umd/test_cu_mem_pool.cpp` (or equivalent) that exercise:
   - Two pools in the same VA Space with non-overlapping sub-ranges.
   - Pool A alloc + Pool B alloc → both succeed with distinct VA.
   - Pool A alloc exceeding `maxSize` → returns `CUDA_ERROR_OUT_OF_MEMORY`.

**UsrLinuxEmu commitment**: We will provide `sim_mem_pool_props_t.va_base` and `va_limit` fields populated at pool creation. Implementation tracked in `tasks.md §2.3` (after our Fix-2 internal revision).

### B-3: fence_id lifecycle migration plan incomplete

**Status**: 🔴 BLOCKER

**Observed**: TaskRunner's `phase3-stream-capture-design.md §Asynchronous Semantics` (assumed location; verify on TaskRunner side) and the cross-repo PR §3.1.3 do not specify how `cuGraphLaunch` returning a fence token maps to the UsrLinuxEmu IOCTL fence_id.

**UsrLinuxEmu existing fence_id mechanism** (verified):

```
$ grep -n "fence" plugins/gpu_driver/drv/gpu_drm_driver.cpp | head -5
212:        u64 fence_id = 0;
213:        int ret = hal_fence_create(self->hal_, &fence_id);
```

All current fence_ids are allocated by `hal_fence_create()` in the HAL layer, range `[1, ...)`.

**UsrLinuxEmu decision** (issued 2026-07-05): We adopt **Option A — minimal intrusion**:

| Layer | fence_id range | Allocation point |
|-------|----------------|------------------|
| HAL / driver (existing) | `[1, (1 << 32) - 1]` | `hal_fence_create()` (unchanged) |
| Sim (new, Phase 3.1/3.2) | `[(1 << 32), INT64_MAX]` | new `sim_fence_id_alloc()` |

`wait_fence(fence_id)` dispatches based on range:
- `< (1 << 32)` → `hal_fence_read()` (existing path)
- `>= (1 << 32)` → `sim_fence_id_check()` (new path)

**Required action from TaskRunner**:

1. In the GpuDriverClient forwarding implementation, **do not** assume fence_id fits in `uint32_t`. Use `int64_t` for all fence_id-typed return values.
2. In the shim layer (`src/umd/libcuda_shim/cu_graph.cpp`, planned for Step 3), `cuGraphLaunch` SHALL return a `CUgraphExec` launch token that wraps the `int64_t` fence_id internally. CUDA runtime API contract uses `cudaError_t` return; fence completion is checked via `cudaStreamSynchronize` polling our `wait_fence` IOCTL.
3. Verify the existing `test_gpu_fence_return_standalone` test (UsrLinuxEmu side, `build/bin/test_gpu_fence_return_standalone`) does not break with the new range split. Our commit will keep HAL fence_id range unchanged for backward compat.

**UsrLinuxEmu commitment**:
- New `plugins/gpu_driver/sim/fence_id.{h,cpp}` exposing `sim_fence_id_alloc()`.
- New `tests/test_fence_id_lifecycle_standalone.cpp` with ≥6 cases (see our `tasks.md §5.6` after Fix-1).
- `GPU_IOCTL_WAIT_FENCE` handler updated to dispatch by range.

## 3. MUST-FIX Issues (must resolve during Step 3 implementation)

### F-1: capture mode range clarification

**Observed**: TaskRunner spec uses `CUstreamCaptureMode` enum (GLOBAL=0, THREAD_LOCAL=1, RELAXED=2).

**UsrLinuxEmu decision**: Phase 3.1 will only accept `SIM_CAPTURE_MODE_GLOBAL (0)`. Other modes return `-EINVAL`.

**Required from TaskRunner**: In Phase 3.1 shim, ensure `cuStreamBeginCapture(mode)` rejects `THREAD_LOCAL` / `RELAXED` with `cudaError_t::cudaErrorNotSupported` (or equivalent). Do not silently fall back to GLOBAL.

### F-2: pool attr value blob layout

**Observed**: TaskRunner's `phase3-mempool-design.md §Pool Attributes` likely uses `cuMemPoolSetAttribute(pool, attr, value)` where `value` is a typed pointer (e.g., `uint64_t*` for releaseThreshold).

**UsrLinuxEmu IOCTL struct**:

```c
struct gpu_mem_pool_attr_args {
  uint64_t pool_handle;
  uint32_t attr;
  uint32_t _reserved;      /* padding, must be 0 */
  uint64_t value[4];       /* 32-byte in/out blob */
};
```

**Attribute layout**:

| attr | value_size | value[0] | value[1..3] |
|------|------------|----------|-------------|
| `RELEASE_THRESHOLD (1)` | 8 | uint64_t release_threshold (bytes) | must be 0 |
| `REUSE_FOLLOW_EVENT_DEPS (2)` | 4 | uint32_t enable (0/1) | must be 0 |

**Required from TaskRunner**: In GpuDriverClient `mem_pool_set_attr` / `get_attr` forwarding, serialize the typed `value` pointer into the `value[0]` slot + set `_reserved=0` + pad the rest with zeros.

### F-3: kernargs=0 semantics

**Observed**: TaskRunner's shim likely creates kernel nodes with `kernargs = nullptr` for parameterless kernels.

**UsrLinuxEmu decision**: `kernargs_bo_handle == 0` is valid and means "no kernargs BO". Driver SHALL NOT validate against the BO table for value 0.

**Required from TaskRunner**: In `cuGraphAddKernelNode` shim, set `kernargs_bo_handle = 0` when `kernelParams == nullptr`. No special handling needed for non-zero BO handles (driver validates existence).

### F-4: int64_t return convention

**Observed**: Three sim primitives return `int64_t` instead of `int`:
- `sim_graph_launch` → fence_id or negative errno
- `sim_mem_pool_alloc_async` → fence_id or negative errno
- `sim_mem_pool_free_async` → fence_id or negative errno

**Convention**: `< 0` → errno (e.g., `-EINVAL`); `>= (1 << 32)` → valid fence_id.

**Required from TaskRunner**: GpuDriverClient forwarding methods wrapping these three SHALL declare return type `int64_t` and propagate negative errno verbatim to caller. Do NOT cast to `int`.

## 4. NICE-TO-HAVE (建议改进)

### N-1: Underscore vs camelCase naming

TaskRunner code uses `camelCase` (per AGENTS.md naming convention). Our sim C primitives use `snake_case`. This is correct (C-ABI convention), but please ensure the **GpuDriverClient wrappers** use `camelCase` for backward compatibility with existing methods (`submitBatch`, `createQueue`, etc.).

### N-2: Test binary naming convention

Suggested test binary names on TaskRunner side:
- `test_cu_stream_capture_standalone` (mirrors UsrLinuxEmu `test_sim_stream_capture_standalone`)
- `test_cu_graph_standalone`
- `test_cu_mem_pool_standalone`
- `test_gpu_driver_client_phase31_standalone` (consolidated 15 forwarding tests)

### N-3: Cross-repo PR diff strategy

For Step 3 (TaskRunner GpuDriverClient + shim implementation), please **split into 3 separate commits**:

1. `feat(igpu_driver): 15-method no-op extension` (Step 1, already done)
2. `feat(gpu_driver_client): 15 forwarding methods` (Step 3a)
3. `feat(umd): cuStreamCapture/CUgraph/cuMemPool shim` (Step 3b)

This enables UsrLinuxEmu submodule bump to pick specific commits.

### N-4: Documentation sync frequency

Suggest updating both `plans/sync-plan.md` (TaskRunner) and UsrLinuxEmu `docs/07-integration/` after each Step completes (1, 2, 3, 4). Current sync happens only at major milestones.

## 5. Asks for TaskRunner Team

| # | Ask | Owner | Deadline |
|---|-----|-------|----------|
| 1 | Confirm acceptance of the 3 BLOCKER fixes (B-1, B-2, B-3) | TaskRunner owner | 2026-07-08 |
| 2 | Update `phase3-stream-capture-design.md` to reference `GpuQueueEmu::submit` | TaskRunner owner | 2026-07-08 |
| 3 | Update `phase3-mempool-design.md` to specify VA sub-range (Option B) | TaskRunner owner | 2026-07-08 |
| 4 | Add fence_id range split to Phase 3.1 spec §Asynchronous Semantics | TaskRunner owner | 2026-07-08 |
| 5 | Update `cross-repo-prs/2026-07-05-phase3-1-stream-mempool-coordination.md` §3.1.3 + §3.2.3 to reflect decisions | TaskRunner owner | 2026-07-09 |

After TaskRunner confirms (1) and updates (2)-(5), UsrLinuxEmu Architecture Team will:
1. Apply our internal Fix-1 through Fix-14 (see `openspec/changes/2026-07-05-sim-stream-primitive-support/fix-steps.md`)
2. Promote OpenSpec change status from `🔄 PROPOSED` to `✅ ACCEPTED`
3. Schedule Step 2 merge to UsrLinuxEmu `main`

## 6. Cross-References

- UsrLinuxEmu OpenSpec change: [`openspec/changes/2026-07-05-sim-stream-primitive-support/`](../../../UsrLinuxEmu/openspec/changes/2026-07-05-sim-stream-primitive-support/proposal.md)
- UsrLinuxEmu review report: [`openspec/changes/2026-07-05-sim-stream-primitive-support/taskrunner-feedback.md`](../../../UsrLinuxEmu/openspec/changes/2026-07-05-sim-stream-primitive-support/taskrunner-feedback.md) (mirror)
- UsrLinuxEmu fix steps: [`openspec/changes/2026-07-05-sim-stream-primitive-support/fix-steps.md`](../../../UsrLinuxEmu/openspec/changes/2026-07-05-sim-stream-primitive-support/fix-steps.md)
- TaskRunner source PR (this is responding to): [`2026-07-05-phase3-1-stream-mempool-coordination.md`](2026-07-05-phase3-1-stream-mempool-coordination.md)
- TaskRunner Phase 3.1 spec: [`../specs/2026-07-02-phase3-stream-capture-design.md`](../specs/2026-07-02-phase3-stream-capture-design.md)
- TaskRunner Phase 3.2 spec: [`../specs/2026-07-02-phase3-mempool-design.md`](../specs/2026-07-02-phase3-mempool-design.md)

## 7. Sync Protocol Acknowledgment

Per `AGENTS.md §跨仓工作原则` and `ADR-035 R5.1`, after TaskRunner addresses these 3 BLOCKERs:

1. TaskRunner commits + pushes fixes to its `main`
2. UsrLinuxEmu updates `external/TaskRunner` submodule pointer
3. UsrLinuxEmu commits submodule bump + OpenSpec ACCEPT transition
4. Both teams re-run their respective regression suites

We aim for the Step 2 (UsrLinuxEmu sim + IOCTL merge) to land on `main` by **2026-07-15**, with Step 3 (TaskRunner shim) to follow by **2026-07-22**.

---

**Document version**: 1.0 (2026-07-05)
**Next revision**: After TaskRunner addresses BLOCKERs (target 2026-07-09)
**Contact**: UsrLinuxEmu Architecture Team via PR review or this cross-repo PR thread