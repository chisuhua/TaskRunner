# Cross-Repo Coordination: TaskRunner Phase 3.1 (Stream Capture + Graph) → UsrLinuxEmu sim Primitives

> **From**: TaskRunner owner (Sisyphus)
> **To**: UsrLinuxEmu Architecture Team
> **Date**: 2026-07-05
> **Type**: Backend support request for upstream UMD-evolution roadmap
> **Priority**: P0 (Phase 3 kickoff trigger condition met)
> **Symmetry**: Mirrored on UsrLinuxEmu side as `openspec/changes/2026-07-05-sim-stream-primitive-support/`

## 1. Executive Summary

TaskRunner's umd-evolution roadmap Phase 3 ([`docs/umd-evolution/roadmap/phase-3-deferred.md`](../../umd-evolution/roadmap/phase-3-deferred.md), [`docs/superpowers/plans/2026-07-02-phase3-prep-design-notes.md`](../../superpowers/plans/2026-07-02-phase3-prep-design-notes.md)) has **trigger condition 1 met**: UsrLinuxEmu Stage 1.4 (KFD portability + Tier-2 runtime penetration) completed on 2026-07-04 (commits `80f6a44` + `9378153`).

TaskRunner is preparing to launch **Phase 3.1** (Stream Capture + CUDA Graph) and **Phase 3.2** (Memory Pool). Both P0 sub-phases require **new sim primitives in UsrLinuxEmu** that are currently absent. This PR describes the backend support we are requesting from the UsrLinuxEmu team.

**Effort asymmetry**:
- TaskRunner side (frontend + IGpuDriver stub): 1.5-2 weeks
- UsrLinuxEmu side (sim primitives + IOCTL numbering): 0.5-1 week per request
- **Critical path**: UsrLinuxEmu side — Phase 3.1/3.2 cannot meaningfully start without backend primitives

## 2. Trigger Condition Met — Why Now

| # | Trigger Condition | Status |
|---|-------------------|--------|
| 1 | UsrLinuxEmu Stage 1.4 starts requiring cuStream/cuMemPool APIs | ✅ **MET** (Stage 1.4 Tier-1 + Tier-2 done, 2026-07-04) |
| 2 | External demand (bug report / user request) | ⏳ None |
| 3 | CI gap (tests requiring non-stub APIs) | ⏳ None |
| 4 | Time pressure (4+ weeks idle) | ⏳ 2 days |

> Phase 3 prep design doc ([`2026-07-02-phase3-prep-design-notes.md`](../../superpowers/plans/2026-07-02-phase3-prep-design-notes.md)) status: DRAFT — will be promoted to ACTIVE upon UsrLinuxEmu agreement to proceed with this coordination request.

## 3. Backend Primitives Requested

### 3.1 For Phase 3.1 — Stream Capture + CUDA Graph

**Priority**: P0 (per [`phase-3-prep-design-notes.md`](../../superpowers/plans/2026-07-02-phase3-prep-design-notes.md))
**Phase**: 3.1
**TaskRunner spec**: [`docs/superpowers/specs/2026-07-02-phase3-stream-capture-design.md`](../../superpowers/specs/2026-07-02-phase3-stream-capture-design.md) (DRAFT, 347 lines)
**Backend dependency**: UsrLinuxEmu Stage 1.4 (✅ met)

#### 3.1.1 sim-level primitives needed

Currently absent from `plugins/gpu_driver/sim/`:

| Primitive | Header location | Purpose |
|-----------|-----------------|---------|
| `sim_stream_capture_start(stream_id, mode)` | New: `sim/stream_capture.h` | Begin recording operations on a stream |
| `sim_stream_capture_end(stream_id, out_graph)` | New: `sim/stream_capture.h` | Stop recording and emit graph metadata |
| `sim_stream_capture_status(stream_id)` | New: `sim/stream_capture.h` | Query current capture state (NONE/ACTIVE/INVALID) |
| `sim_graph_create(out_graph)` | New: `sim/graph.h` | Create empty DAG node container |
| `sim_graph_destroy(graph_id)` | New: `sim/graph.h` | Free DAG metadata |
| `sim_graph_add_kernel_node(graph, kernel, args)` | New: `sim/graph.h` | Append kernel-launch node to DAG |
| `sim_graph_add_memcpy_node(graph, src, dst, size, kind)` | New: `sim/graph.h` | Append memcpy node to DAG |
| `sim_graph_instantiate(graph_id, out_exec)` | New: `sim/graph.h` | Create executable instance (validation only — no real launch) |
| `sim_graph_launch(exec_id, stream_id)` | New: `sim/graph.h` | Launch graph (no-op semantics, fence only) |

**Real semantics requirement**: capture nodes must record enough metadata to be replayed through `sim_pfh_inject_fault` / `sim_pm_migrate_to_*` when replayed. Specifically, kernel args + BO handles must be queryable.

> **UsrLinuxEmu B-1 BLOCKER 决策（2026-07-05）**:TaskRunner spec 必须避免引用 `GpuQueueEmu::submit_batch` 或 `GpuQueueEmu::enqueue`（**不存在**）。实际只有 `GpuQueueEmu::submit(uint64_t gpfifo_addr, uint32_t entry_count)`（`gpu_queue_emu.h:113`）。
>
> **集成路径**:`gpu_graph_launch_args` 携带 `exec_handle` + `stream_id` → driver-side handler (`gpu_drm_driver.cpp`) 转 `gpfifo_addr + entry_count` → 调 `GpuQueueEmu::submit`。
>
> **GpuDriverClient 实施要求（Step 3 PR 时）**:无任何 TaskRunner shim 代码假设 `submit_batch` 或 `enqueue` API。

#### 3.1.2 IOCTL additions needed

Following the existing `gpu_ioctl.h` numbering convention:

| IOCTL number | Name | Purpose |
|--------------|------|---------|
| 0x50 | `GPU_IOCTL_STREAM_CAPTURE_BEGIN` | Begin stream capture |
| 0x51 | `GPU_IOCTL_STREAM_CAPTURE_END` | End stream capture, return graph handle |
| 0x52 | `GPU_IOCTL_STREAM_CAPTURE_STATUS` | Query capture status |
| 0x53 | `GPU_IOCTL_GRAPH_CREATE` | Create graph |
| 0x54 | `GPU_IOCTL_GRAPH_DESTROY` | Destroy graph |
| 0x55 | `GPU_IOCTL_GRAPH_ADD_KERNEL_NODE` | Add kernel-launch node |
| 0x56 | `GPU_IOCTL_GRAPH_ADD_MEMCPY_NODE` | Add memcpy node |
| 0x57 | `GPU_IOCTL_GRAPH_INSTANTIATE` | Instantiate graph for execution |
| 0x58 | `GPU_IOCTL_GRAPH_LAUNCH` | Launch instantiated graph |
| 0x59 | `GPU_IOCTL_GRAPH_DESTROY_EXEC` | Destroy graph executable |

> **Numbering policy (revised)**: Following the established `0x40-0x47` reserved range for KFD (1.2 stage) and `0x30-0x32` for VA Space, the **0x50-0x59 range is reserved for graph/capture**, and **0x60-0x67 range is reserved for memory pool**. The **0x70-0x7F range** is explicitly reserved for future use (not allocated by this change). TaskRunner requests these ranges to be formally documented in `gpu_ioctl.h` with reservation comments.

#### 3.1.3 IGpuDriver method extensions (extended after Oracle review)

We propose extending IGpuDriver ([`include/shared/igpu_driver.hpp`](../../../include/shared/igpu_driver.hpp)) from 31 → 46 methods (adding **10 methods** for Phase 3.1, not just 4 — see Oracle C1 finding: full coverage of all 10 graph/capture IOCTLs through IGpuDriver, no abstraction leak).

```cpp
// ============================================================
// Phase 3.1 Stream Capture / Graph (10, 虚函数 默认 no-op, 非纯虚)
// ============================================================
//
// 所有方法均为 虚函数 + 默认 no-op 实现（非纯虚），保证：
// - CudaStub / MockGpuDriver 不强制 override（向后兼容）
// - GpuDriverClient 可选择性 override 转发到 IOCTL
// 注释澄清：之前文档误写"纯虚占位"，实际为非纯虚 + 默认实现。

/**
 * @brief Query stream capture status (Phase 3.1)
 * @param stream_id 流 ID
 * @param[out] status_out SIM_STREAM_CAPTURE_STATUS_* (NONE/ACTIVE/INVALID)
 * @return 0 成功, -1 失败
 */
virtual int stream_capture_status(uint32_t stream_id, uint32_t* status_out) { return -1; }

/**
 * @brief Create empty CUDA graph (Phase 3.1)
 * @param[out] graph_handle_out graph handle (>=1)
 * @return 0 成功, -1 失败
 */
virtual int graph_create(uint64_t* graph_handle_out) { return -1; }

/**
 * @brief Destroy CUDA graph (Phase 3.1)
 * @param graph_handle graph handle
 * @return 0 成功, -1 失败
 */
virtual int graph_destroy(uint64_t graph_handle) { return -1; }

/**
 * @brief Add kernel-launch node to graph (Phase 3.1)
 * @param graph_handle graph handle
 * @param kernel_index kernel registry index
 * @param grid_x/y/z, block_x/y/z launch dims
 * @param kernargs_bo_handle BO handle containing serialized kernel args
 * @return 0 成功, -1 失败
 */
virtual int graph_add_kernel_node(uint64_t graph_handle, uint32_t kernel_index,
                                   uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
                                   uint32_t block_x, uint32_t block_y, uint32_t block_z,
                                   uint64_t kernargs_bo_handle) { return -1; }

/**
 * @brief Add memcpy node to graph (Phase 3.1)
 * @param graph_handle graph handle
 * @param src_va source virtual address
 * @param dst_va destination virtual address
 * @param size bytes to copy
 * @param is_h2d 1=H2D, 0=D2H
 * @return 0 成功, -1 失败
 */
virtual int graph_add_memcpy_node(uint64_t graph_handle,
                                   uint64_t src_va, uint64_t dst_va,
                                   uint64_t size, uint32_t is_h2d) { return -1; }

/**
 * @brief Instantiate graph into executable (Phase 3.1)
 * @param graph_handle graph handle
 * @param[out] exec_handle_out executable handle (>=1)
 * @return 0 成功, -1 失败
 */
virtual int graph_instantiate(uint64_t graph_handle, uint64_t* exec_handle_out) { return -1; }

/**
 * @brief Launch instantiated graph (Phase 3.1)
 * @param graph_exec_handle executable graph handle
 * @param stream_id 流 ID
 * @return fence_id (>=1), 0 表示成功但无 fence, -1 失败
 */
virtual int64_t submit_graph(uint64_t graph_exec_handle, uint32_t stream_id) { return -1; }

/**
 * @brief Destroy graph executable (Phase 3.1)
 * @param graph_exec_handle executable graph handle
 * @return 0 成功, -1 失败
 */
virtual int destroy_graph_exec(uint64_t graph_exec_handle) { return -1; }

/**
 * @brief Begin stream capture (Phase 3.1)
 * @param stream_id 流 ID
 * @param mode capture mode (CU_STREAM_CAPTURE_MODE_*)
 * @return 0 成功, -1 失败
 */
virtual int stream_capture_begin(uint32_t stream_id, uint32_t mode) { return -1; }

/**
 * @brief End stream capture, return graph handle (Phase 3.1)
 * @param stream_id 流 ID
 * @param[out] graph_handle_out 返回 graph handle
 * @return 0 成功, -1 失败
 */
virtual int stream_capture_end(uint32_t stream_id, uint64_t* graph_handle_out) { return -1; }
```

> **Design rationale (revised)**: Oracle review (C1) identified that the original 4-method extension left 6 IOCTLs (status, create, destroy, add_kernel_node, add_memcpy_node, instantiate) without IGpuDriver methods, forcing the shim layer to either call CudaStub directly (abstraction leak) or downcast GpuDriverClient (abstraction leak). The revised 10-method extension keeps IGpuDriver as the **single abstraction boundary** between TaskRunner and the GPU driver (per ADR-032). Default implementations preserve backward compatibility — CudaStub / MockGpuDriver can override as needed; GpuDriverClient forwards all 10 to new IOCTLs.

### 3.2 For Phase 3.2 — Memory Pool (cuMemPool*)

**Priority**: P0
**Phase**: 3.2
**TaskRunner spec**: [`docs/superpowers/specs/2026-07-02-phase3-mempool-design.md`](../../superpowers/specs/2026-07-02-phase3-mempool-design.md) (DRAFT, 227 lines)
**Backend dependency**: UsrLinuxEmu Stage 1.3 UVM (✅ met per `2026-07-04-stage-1-3-uvm-hmm`)

#### 3.2.1 sim-level primitives needed

| Primitive | Header location | Purpose |
|-----------|-----------------|---------|
| `sim_mem_pool_create(props, out_pool)` | New: `sim/mem_pool.h` | Create memory pool from VA range |
| `sim_mem_pool_destroy(pool_handle)` | New: `sim/mem_pool.h` | Destroy pool |
| `sim_mem_pool_alloc(pool, size, out_va)` | New: `sim/mem_pool.h` | Allocate from pool (sync, returns VA) |
| `sim_mem_pool_alloc_async(pool, size, stream_id, out_va)` | New: `sim/mem_pool.h` | Async alloc (returns fence) |
| `sim_mem_pool_free_async(va, stream_id)` | New: `sim/mem_pool.h` | Async free (returns fence) |
| `sim_mem_pool_set_attr(pool, attr, value)` | New: `sim/mem_pool.h` | Set pool attribute (release threshold, reuse policy) |
| `sim_mem_pool_get_attr(pool, attr, out_value)` | New: `sim/mem_pool.h` | Get pool attribute |
| `sim_mem_pool_trim(pool, min_bytes)` | New: `sim/mem_pool.h` | Trim pool to retain min_bytes |

**Real semantics requirement**: pool allocations must integrate with existing `alloc_bo` (`libgpu_core/gpu_buddy`); pool must support reuse following event dependencies (for `CU_MEMPOOL_ATTR_REUSE_FOLLOW_EVENT_DEPENDENCIES` attribute).

> **UsrLinuxEmu B-2 BLOCKER 决策（2026-07-05）**:VA 范围架构采用 **Option B（VA 子范围预留）**。pool 在 VA Space 内预留 `maxSize` 大小子范围,`sim_mem_pool_props_t` 加 `va_base` / `va_limit` 字段。
>
> **禁用 Option A**（pool 作为 `gpu_buddy` 薄包装）— UsrLinuxEmu 反馈 B-2:Option A 无法强制 `maxSize` 上限。
>
> **TaskRunner shim 要求**:`GpuDriverClient::mem_pool_*` 将 `props.maxSize` 视为容量上限;接收 `va_base` / `va_limit` 字段;测试覆盖两 pool 不重叠 + 超额返回 `CUDA_ERROR_OUT_OF_MEMORY`。

#### 3.2.2 IOCTL additions needed

| IOCTL number | Name | Purpose |
|--------------|------|---------|
| 0x60 | `GPU_IOCTL_MEM_POOL_CREATE` | Create pool |
| 0x61 | `GPU_IOCTL_MEM_POOL_DESTROY` | Destroy pool |
| 0x62 | `GPU_IOCTL_MEM_POOL_ALLOC` | Allocate from pool (sync) |
| 0x63 | `GPU_IOCTL_MEM_POOL_ALLOC_ASYNC` | Async allocate (returns fence) |
| 0x64 | `GPU_IOCTL_MEM_POOL_FREE_ASYNC` | Async free (returns fence) |
| 0x65 | `GPU_IOCTL_MEM_POOL_SET_ATTR` | Set attribute |
| 0x66 | `GPU_IOCTL_MEM_POOL_GET_ATTR` | Get attribute |
| 0x67 | `GPU_IOCTL_MEM_POOL_TRIM` | Trim pool |

> **Numbering policy**: 0x60-0x67 range reserved for memory pool. Continues from existing 0x50-0x59 (graph/capture). 0x70-0x7F reserved for future use.

#### 3.2.3 IGpuDriver method extensions (extended after Oracle review)

Adding 5 more methods to IGpuDriver (41 → 46 total, after Phase 3.1's 10 methods):

```cpp
// ============================================================
// Phase 3.2 Memory Pool (5, 虚函数 默认 no-op, 非纯虚)
// ============================================================
//
// 所有方法均为 虚函数 + 默认 no-op 实现（非纯虚），保证向后兼容。

/**
 * @brief Create memory pool (Phase 3.2)
 * @param va_space_handle owning VA Space handle
 * @param size pool total size in bytes
 * @param flags pool flags (CU_MEMPOOL_*)
 * @param[out] pool_handle_out pool handle (>=1)
 * @return 0 成功, -1 失败
 */
virtual int mem_pool_create(uint64_t va_space_handle, uint64_t size,
                             uint32_t flags, uint64_t* pool_handle_out) { return -1; }

/**
 * @brief Destroy memory pool (Phase 3.2)
 * @param pool_handle pool handle
 * @return 0 成功, -1 失败
 */
virtual int mem_pool_destroy(uint64_t pool_handle) { return -1; }

/**
 * @brief Synchronous allocation from pool (Phase 3.2)
 * @param pool_handle pool handle
 * @param size allocation size in bytes
 * @param[out] va_out returned virtual address
 * @return 0 成功, -1 失败
 */
virtual int mem_pool_alloc(uint64_t pool_handle, uint64_t size,
                            uint64_t* va_out) { return -1; }

/**
 * @brief Asynchronous allocation from pool (Phase 3.2)
 * @param pool_handle pool handle
 * @param size allocation size in bytes
 * @param stream_id target stream
 * @param[out] va_out returned virtual address
 * @return fence_id (>=1), 0 表示成功但无 fence, -1 失败
 */
virtual int64_t mem_pool_alloc_async(uint64_t pool_handle, uint64_t size,
                                      uint32_t stream_id, uint64_t* va_out) { return -1; }

/**
 * @brief Asynchronous free (Phase 3.2)
 * @param va virtual address to free
 * @param stream_id target stream
 * @return fence_id (>=1), 0 表示成功但无 fence, -1 失败
 */
virtual int64_t mem_pool_free_async(uint64_t va, uint32_t stream_id) { return -1; }
```

> **Note on missing mempool methods**: Oracle review identified that 3 mempool IOCTLs (set_attr, get_attr, trim — 0x65/0x66/0x67) do **not** need separate IGpuDriver methods because the attr/trim operations can be expressed through existing `mem_pool_alloc` / `mem_pool_destroy` semantics in Phase 3.2's initial scope. If a later Phase 3.x requires explicit attribute control, add 3 more methods (Phase 3.2 → 49 total).

**Final method count breakdown**:

| Phase | New methods | Cumulative |
|-------|-------------|------------|
| Current | — | 31 |
| Phase 3.1 (Stream Capture + Graph) | 10 | 41 |
| Phase 3.2 (Memory Pool core) | 5 | 46 |
| Phase 3.2 extension (set_attr/get_attr/trim, if needed) | +3 | 49 |

## 4. Effort Estimate (revised after Oracle review)

| Component | Scope | Effort |
|-----------|-------|--------|
| **UsrLinuxEmu side** | sim primitives + 18 IOCTL definitions + IOCTL handlers in `gpu_drm_driver.cpp` | ~1 week |
| | Sim test coverage (happy + error paths) | +0.5 week |
| | Update ADR-015 IOCTL numbering table (0x44-0x47 + 0x50-0x67) | +0.5 day |
| **TaskRunner side** | IGpuDriver method extensions (10 graph/capture + 5 mempool = **15 methods**, default no-op) | 1 day |
| | GpuDriverClient 15 forwarding methods (depends on UsrLinuxEmu IOCTL #defines) | 1 day |
| | cuStreamCapture/CUgraph API shim layer | 1 week |
| | cuMemPool* API shim layer | 1 week |
| | shim E2E test cases (Phase 1.7 style) | 1 week |
| | docs-audit + ABI symbol count updates | 0.5 day |

**Total**: 2.5-3.5 weeks end-to-end, with strict 4-step coordination sequence (see §5).

## 5. Dependency & Sequencing (revised after Oracle C5)

**Critical**: The previous "Week 1 parallel" plan had a build-break risk — TaskRunner's GpuDriverClient forwarding methods reference `GPU_IOCTL_*` #defines in UsrLinuxEmu's `gpu_ioctl.h` (accessed via symlink). If TaskRunner merges GpuDriverClient forwarding **before** UsrLinuxEmu merges the IOCTL #defines, TaskRunner **fails to compile**.

**Correct 4-step coordination sequence**:

```
Step 0 (this week) — preparation:
  - UsrLinuxEmu: review + accept this coordination request (§8 decisions)
  - TaskRunner: promote phase-3-prep-design-notes.md from DRAFT → ACTIVE

Step 1 (TaskRunner only, no cross-dep) — IGpuDriver no-op extension:
  TaskRunner:
    - Extend IGpuDriver with 15 new methods (10 graph/capture + 5 mempool)
    - ALL methods are 虚函数 with 默认 no-op 实现 (NOT 纯虚) — backward compat
    - NO references to GPU_IOCTL_*  #defines
    - Commit + push to TaskRunner main

Step 2 (UsrLinuxEmu only, no cross-dep) — backend primitives:
  UsrLinuxEmu:
    - Add sim_stream_*.h / sim_graph_*.h / sim_mem_pool_*.h
    - Add 18 IOCTL definitions (0x50-0x67) in gpu_ioctl.h with full struct defs
    - Add 18 IOCTL handlers in gpu_drm_driver.cpp
    - Update ADR-015 IOCTL numbering table
    - Add sim test coverage
    - Commit + push to UsrLinuxEmu main

Step 3 (TaskRunner, depends on Step 2) — GpuDriverClient forwarding:
  TaskRunner:
    - GpuDriverClient overrides 15 methods to call new GPU_IOCTL_* (via symlink to UsrLinuxEmu gpu_ioctl.h)
    - cuStreamCapture/CUgraph shim layer
    - cuMemPool* shim layer
    - shim E2E tests
    - Commit + push to TaskRunner main

Step 4 (UsrLinuxEmu) — submodule sync:
  UsrLinuxEmu:
    - Bump submodule pointer to TaskRunner Step 3 commit
    - Verify build + all regression tests pass
    - Commit submodule bump to UsrLinuxEmu main
```

**Timeline**:

| Step | Duration | Owner | Depends on |
|------|----------|-------|------------|
| Step 0 | 2-3 days | Both | — |
| Step 1 | 1 day | TaskRunner | Step 0 |
| Step 2 | 1.5 weeks | UsrLinuxEmu | Step 0 (can start parallel with Step 1) |
| Step 3 | 1.5 weeks | TaskRunner | Step 2 |
| Step 4 | 0.5 day | UsrLinuxEmu | Step 3 |

**Critical path**: Step 2 → Step 3 → Step 4 (2-2.5 weeks)
**Parallelism**: Step 1 (TaskRunner) can run parallel with Step 2 (UsrLinuxEmu) — no overlap.

## 6. Risk Assessment (revised)

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| UsrLinuxEmu Stage 2 (multi-device) preempts sim work | Medium | Medium | Stage 2 work is orthogonal (plugins/net_driver, plugins/storage_driver) — no overlap with sim primitives |
| IOCTL range reservation conflicts with future work | Low | Low | Reserve 0x50-0x67 (graph/capture/mempool) + 0x70-0x7F (future); document in `gpu_ioctl.h` |
| IGpuDriver ABI breakage | Low | High | All 15 new methods are 虚函数 with 默认 no-op 实现 (NOT 纯虚) — old code compiles unchanged |
| Sim primitives don't match real semantics | Medium | High | Use `sim_pfh_inject_fault` / `sim_pm_migrate_*` as reference; integration test against Stage 1.4 Tier-2 runtime |
| Tier-2 G1-G4 boundary regression | Low | Medium | Run `test_uvm_drm_lifecycle_standalone` after each sim primitive addition (Oracle M4: per-primitive, not batch) |
| Cross-repo merge out-of-order (build break) | High | High | **Strict 4-step coordination sequence** (Step 1 → 2 → 3 → 4); documented in §5 |
| IOCTL `_IOW` / `_IOWR` misdirection (silent data loss) | Medium | High | All 10 IOCTLs returning data explicitly use `_IOWR`; documented in `design.md §IOCTL Directions` (Oracle H2) |
| sim fence_id and driver fence tracking conflict | Medium | Medium | Unified fence_id generation in `sim_fence_id_alloc()`; documented in `design.md §fence_id Lifecycle` (Oracle H4) |

## 7. Acceptance Criteria (revised)

This coordination request is considered **complete** when:

1. **Backend primitives exist**: All `sim_stream_*`, `sim_graph_*`, `sim_mem_pool_*` headers + .cpp implementations
2. **IOCTLs defined**: `gpu_ioctl.h` has 0x50-0x67 range with **full struct definitions for all 18 IOCTLs** (including `gpu_graph_launch_args` with `fence_id_out` field — Oracle C3 fix)
3. **IGpuDriver extended**: 15 new methods (10 graph/capture + 5 mempool) committed to TaskRunner `main` (Step 1)
4. **GpuDriverClient forwards**: 15 new forwarding methods in TaskRunner's `src/test_fixture/gpu_driver_client.cpp` map to IOCTLs (Step 3, NOT in UsrLinuxEmu change scope)
5. **Sim test coverage**: ≥80% line coverage on new sim primitives, with happy path + ≥1 error path per primitive
6. **Cross-repo E2E test**: Phase 3.1 + 3.2 shim APIs verified through GpuDriverClient (not just CudaStub)
7. **4-step coordination followed**: Step 1 → Step 2 → Step 3 → Step 4 strict sequence (no out-of-order merges)
8. **ADR-015 updated**: IOCTL numbering table includes 0x44-0x47 + 0x50-0x67
9. **TADR-301 updated**: IGpuDriver contract documented at 46-method level

## 8. Open Questions for UsrLinuxEmu Architecture Team

1. **IOCTL range reservation** ⭐: OK to formally reserve **0x50-0x67** for graph/capture/mempool (with 0x70-0x7F reserved for future)?
2. **Sim primitives ownership**: Should `sim_stream_*` / `sim_graph_*` / `sim_mem_pool_*` live in `plugins/gpu_driver/sim/` (alongside `sim_pfh_*`, `sim_pm_*`) or a new `plugins/gpu_driver/sim/cuda_emu/` subdirectory?
3. **Pool VA range reuse**: Should `sim_mem_pool_alloc` integrate with existing `alloc_bo` (libgpu_core/gpu_buddy) or maintain separate pool VA ranges?
4. **Graph capture semantics**: Do graph nodes need to record full kernel arg serialization (for `sim_pfh_inject_fault` replay) or just BO handles + symbolic references?
5. **Stage 2 coordination**: Will `Stage 2 multi-device` work overlap with Phase 3.1/3.2 backend support, or are they independent work streams?
6. **IOCTL `_IOW` / `_IOWR` direction** (Oracle H2): Confirm the proposed direction spec in `design.md §IOCTL Directions` table
7. **fence_id lifecycle** (Oracle H4): Confirm `design.md §fence_id Lifecycle` definition is acceptable
8. **Capture mode enum** (Oracle H5): Confirm `sim_capture_mode_t` enum definition (THREAD_LOCAL / GLOBAL / RELAXED)

## 9. Mirrored Change

This request has a corresponding UsrLinuxEmu-side change proposal:

- **Path**: `UsrLinuxEmu/openspec/changes/2026-07-05-sim-stream-primitive-support/`
- **Files**: `proposal.md`, `design.md`, `tasks.md`, `specs/sim-stream-primitive-support/spec.md`
- **Status**: PROPOSED (awaits UsrLinuxEmu Architecture Team review)

## 10. References

### TaskRunner side

- [`docs/umd-evolution/roadmap/phase-3-deferred.md`](../../umd-evolution/roadmap/phase-3-deferred.md) — primary deferred roadmap
- [`docs/umd-evolution/roadmap/current-status.md`](../../umd-evolution/roadmap/current-status.md) — master snapshot
- [`docs/superpowers/plans/2026-07-02-phase3-prep-design-notes.md`](../../superpowers/plans/2026-07-02-phase3-prep-design-notes.md) — kickoff design notes
- [`docs/superpowers/specs/2026-07-02-phase3-stream-capture-design.md`](../../superpowers/specs/2026-07-02-phase3-stream-capture-design.md) — Phase 3.1 design (DRAFT)
- [`docs/superpowers/specs/2026-07-02-phase3-mempool-design.md`](../../superpowers/specs/2026-07-02-phase3-mempool-design.md) — Phase 3.2 design (DRAFT)
- [`docs/shared/adr/tadr-301-igpu-driver-contract.md`](../../shared/adr/tadr-301-igpu-driver-contract.md) — current 31-method contract

### UsrLinuxEmu side

- [`docs/roadmap/stage-1-kernel-emu.md`](../../../../UsrLinuxEmu/docs/roadmap/stage-1-kernel-emu.md) — Stage 1 plan (Trigger source)
- [`docs/05-advanced/kfd-portability-boundary.md`](../../../../UsrLinuxEmu/docs/05-advanced/kfd-portability-boundary.md) — Tier-1/Tier-2 boundary SSOT
- [`plugins/gpu_driver/shared/gpu_ioctl.h`](../../../../UsrLinuxEmu/plugins/gpu_driver/shared/gpu_ioctl.h) — current IOCTL numbering
- [`plugins/gpu_driver/sim/`](../../../../UsrLinuxEmu/plugins/gpu_driver/sim/) — existing sim primitives (sim_pfh_*, sim_pm_*)

### Previous coordination examples

- [`docs/superpowers/cross-repo-prs/2026-07-02-h3-followup-fixes.md`](2026-07-02-h3-followup-fixes.md) — minor fix request template
- UsrLinuxEmu `openspec/changes/archive/2026-07-02-taskrunner-umd-backend-enable/` — Phase 1.5 Stretch coordination template

## 11. Suggested Next Steps

**For UsrLinuxEmu Architecture Team**:
1. Review this request + the mirrored `2026-07-05-sim-stream-primitive-support/proposal.md`
2. Decision on IOCTL range reservation (0x50-0x7F?) and sim primitive location
3. Acceptance → creates an `accepted` ADR + an active OpenSpec change

**For TaskRunner owner (after UsrLinuxEmu acceptance)**:
1. Promote `phase-3-prep-design-notes.md` from DRAFT → ACTIVE
2. Begin IGpuDriver **15-method** extension (10 graph/capture + 5 mempool, all 虚函数 + 默认 no-op，非纯虚 — 保持向后兼容)
3. Coordinate weekly sync with UsrLinuxEmu owner

## 12. UsrLinuxEmu Architecture Team Feedback Resolution（2026-07-05）

**反馈来源**:`docs/superpowers/cross-repo-prs/2026-07-05-usrlx-architecture-review.md`（CONDITIONAL ACCEPT，3 BLOCKER + 4 MUST-FIX + 4 NICE）

### 12.1 🔴 BLOCKER 解决方案

| BLOCKER | 决策 | 修订位置 |
|---------|------|---------|
| **B-1** GpuQueueEmu API 失配 | **接受** — 实际只有 `GpuQueueEmu::submit(uint64_t, uint32_t)`。TaskRunner spec 改用此 API，driver-side handler 转 `gpfifo_addr + entry_count` | [`specs/2026-07-02-phase3-stream-capture-design.md §4.5`](../../superpowers/specs/2026-07-02-phase3-stream-capture-design.md) §"与 `IGpuDriver` 的集成 — UsrLinuxEmu B-1 决策" |
| **B-2** Pool VA 范围架构 | **Option B 强制采用** — pool 预留 VA 子范围，加 `va_base` / `va_limit` 字段。**禁用** Option A（无法强制 maxSize） | [`specs/2026-07-02-phase3-mempool-design.md §4.1`](../../superpowers/specs/2026-07-02-phase3-mempool-design.md) §"池大小追踪与 VA 范围" |
| **B-3** fence_id 生命周期迁移 | **Option A 最小侵入** — HAL fence `[1, 1<<32-1]` 不变，sim fence 新增 `[1<<32, INT64_MAX]` 由 `sim_fence_id_alloc()` 分配，`wait_fence` 按范围分发 | [`specs/2026-07-02-phase3-stream-capture-design.md §4.7`](../../superpowers/specs/2026-07-02-phase3-stream-capture-design.md) §"fence_id 范围划分" |

### 12.2 🟡 MUST-FIX 解决方案（Step 3 实施时）

| MUST-FIX | 决策 | 修订位置 |
|----------|------|---------|
| **F-1** capture mode 范围 | **仅接受 GLOBAL (0)**，其他 mode 返回 `cudaErrorNotSupported`/`cudaErrorInvalidValue`（**不**静默 fallback） | `phase3-stream-capture-design.md §4.1` |
| **F-2** attr value blob 布局 | IOCTL struct `value[4]` 32-byte blob:RELEASE_THRESHOLD 用 `value[0]` (uint64_t)，REUSE_FOLLOW_EVENT_DEPS 用 `value[0]` (uint32_t)，其余填 0 | `phase3-mempool-design.md §4.5` §"IOCTL attr value blob 布局" |
| **F-3** kernargs=0 语义 | `kernargs_bo_handle == 0` 表示无参数 kernel，**不**校验 BO 表存在性 | `phase3-stream-capture-design.md §4.8` |
| **F-4** int64_t 返回约定 | 3 个 sim 原语返回 `int64_t`（`submit_graph` / `mem_pool_alloc_async` / `mem_pool_free_async`）。`<0` = errno，`>= (1<<32)` = 有效 fence_id | `phase3-stream-capture-design.md §4.7` |

### 12.3 🟢 NICE（建议改进，待评估）

- **N-1** GpuDriverClient wrapper 用 camelCase (`submitBatch`/`createQueue`) → TaskRunner 内部风格确认中
- **N-2** TaskRunner 测试 binary 命名 `test_cu_stream_capture_standalone` 等 → 待 TaskRunner 团队确认
- **N-3** Step 3 拆 3 commit（IGpuDriver no-op / GpuDriverClient forwarding / shim）→ TaskRunner owner 接受
- **N-4** 每次 Step 完成同步 `plans/sync-plan.md` + `docs/07-integration/` → TaskRunner owner 接受

### 12.4 修订时间线（更新后）

| 日期 | 里程碑 | 责任方 |
|------|--------|--------|
| 2026-07-05 | TaskRunner 收到 UsrLinuxEmu 反馈 ✅ 已完成 | — |
| **2026-07-05** | **TaskRunner 完成 3 BLOCKER 修订（本文档更新）** | TaskRunner ✅ |
| 2026-07-08 | UsrLinuxEmu 收到 TaskRunner 接受确认 | 双向 |
| 2026-07-09 | UsrLinuxEmu 应用 Fix-1 至 Fix-14 | UsrLinuxEmu |
| 2026-07-12 | OpenSpec 状态升级至 ACCEPTED | UsrLinuxEmu |
| 2026-07-15 | **Step 2 merge**（UsrLinuxEmu sim + IOCTL）| UsrLinuxEmu |
| 2026-07-22 | **Step 3 merge**（TaskRunner shim + E2E）| TaskRunner |
| 2026-07-25 | Step 4 submodule bump + 最终回归 | UsrLinuxEmu |

### 12.5 关键决策文档化

下列决策已纳入 Phase 3.1/3.2 spec 的决策编号体系：

- **D-SC-5** Capture mode 限制（GLOBAL only，修订版）
- **D-SC-9** GpuQueueEmu API 集成路径（修订版）
- **D-SC-11** fence_id 范围划分（**新增**）
- **D-SC-12** kernargs_bo_handle=0 语义（**新增**）
- **D-MP-1** Pool VA 范围采用 Option B（修订版）

---

**Last Updated**: 2026-07-05（updated §12 after UsrLinuxEmu Architecture Team review）
**Next Action**: Awaiting UsrLinuxEmu Architecture Team to apply Fix-1~Fix-14 and promote OpenSpec change to ACCEPTED
**Contact**: TaskRunner owner (Sisyphus) → UsrLinuxEmu Architecture Team