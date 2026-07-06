---
SCOPE: shared + umd-evolution
STATUS: PROPOSED
DATE: 2026-07-06
CHANGE: phase3-step3-shim-and-forwarding
RELATED_ADR: [ADR-032](../../../shared/adr/tadr-301-igpu-driver-contract.md), [ADR-035](../../../shared/adr/)
RELATED_UPSTREAM: [UsrLinuxEmu PR #20](https://github.com/chisuhua/UsrLinuxEmu/pull/20) (Step 2, MERGED 2026-07-06)
RELATED_DOWNSTREAM: [TaskRunner 2026-07-05-phase3-1-igpu-driver-extension](https://github.com/chisuhua/TaskRunner) (Step 1, MERGED)
REVISION_NOTE: 2026-07-06 self-review — corrected struct field names, removed fictional submit_ioctl(), adopted self-contained shim pattern.
---

# Capability: phase3-step3-shim

> Bridge between CUDA Runtime API (cuStream*/cuGraph*/cuMemPool*) and UsrLinuxEmu sim primitives.
> Step 3 of 4-step cross-repo coordination (per [coordination PR §5](../../../superpowers/cross-repo-prs/2026-07-05-phase3-1-stream-mempool-coordination.md)).

## ADDED Requirements

### REQ-S3-1: GpuDriverClient 15 Forwarding Methods

The system MUST provide forwarding implementations of all 15 IGpuDriver methods added in Step 1 (commit 21f71c9). Each method MUST inline `ioctl(fd_, GPU_IOCTL_*, &args)` (matching the existing `free_bo` / `wait_fence` pattern), NOT extracting a `submit_ioctl()` helper.

#### Scenario: Forwarding 10 graph/capture methods

- **WHEN** any of `stream_capture_status`, `stream_capture_begin`, `stream_capture_end`, `graph_create`, `graph_destroy`, `graph_add_kernel_node`, `graph_add_memcpy_node`, `graph_instantiate`, `submit_graph`, `destroy_graph_exec` is called
- **THEN** GpuDriverClient MUST dispatch `ioctl(fd_, GPU_IOCTL_*, &args)` to the corresponding IOCTL in range 0x50-0x59
- **AND** return value semantics MUST follow the F-4 convention:
  - `int` return: 0 = success, `<0` = negative errno
  - `int64_t` return (`submit_graph`): `>= 1<<32` = valid sim fence_id, `<0` = negative errno

#### Scenario: Forwarding 5 mempool methods

- **WHEN** any of `mem_pool_create`, `mem_pool_destroy`, `mem_pool_alloc`, `mem_pool_alloc_async`, `mem_pool_free_async` is called
- **THEN** GpuDriverClient MUST dispatch `ioctl(fd_, GPU_IOCTL_*, &args)` to the corresponding IOCTL in range 0x60-0x64
- **AND** `mem_pool_alloc_async` / `mem_pool_free_async` MUST return int64_t per F-4

#### Scenario: MemPool create with H-1 sentinel guard (B-2)

- **GIVEN** `va_space_handle == 0` (H-1 sentinel, see ADR-029)
- **WHEN** `mem_pool_create(0, size, flags, &pool_handle_out)` is called
- **THEN** GpuDriverClient MUST return -1 WITHOUT calling `ioctl()` (H-1 sentinel guard)
- **AND** log to stderr: `[GpuDriverClient] mem_pool_create: rejected H-1 sentinel (va_space_handle=0)`

### REQ-S3-2: struct Field Correctness (compile-blocking)

The GpuDriverClient overrides MUST use the correct field names as defined in `gpu_ioctl.h`:

| Override | IOCTL struct | Critical field names |
|----------|-------------|---------------------|
| `stream_capture_status` | `gpu_stream_capture_status_args` | `.stream_id`, `.status_out` |
| `stream_capture_begin` / `end` | `gpu_stream_capture_args` | `.stream_id`, `.mode`, `.graph_handle_out` |
| `graph_create` | `gpu_graph_create_args` | `.graph_handle_out` |
| `graph_destroy` | `gpu_graph_destroy_args` | `.graph_handle` |
| `graph_add_kernel_node` | `gpu_graph_add_kernel_node_args` | `.graph_handle`, `.kernel_index`, `.grid_x/y/z`, `.block_x/y/z`, `.kernargs_bo_handle` |
| `graph_add_memcpy_node` | `gpu_graph_add_memcpy_node_args` | `.graph_handle`, `.src_va`, `.dst_va`, `.size`, `.is_h2d` |
| `graph_instantiate` | `gpu_graph_instantiate_args` | `.graph_handle`, `.exec_handle_out` |
| `submit_graph` | `gpu_graph_launch_args` | **`.exec_handle`** (NOT `.graph_exec_handle`), `.stream_id`, `.fence_id_out` |
| `destroy_graph_exec` | `gpu_graph_destroy_exec_args` | `.exec_handle` |
| `mem_pool_create` | `gpu_mem_pool_create_args` | **`.props.va_space_handle`** (nested!), `.props.size`, `.props.flags`, `.pool_handle_out` |
| `mem_pool_destroy` | `gpu_mem_pool_destroy_args` | `.pool_handle` |
| `mem_pool_alloc` | `gpu_mem_pool_alloc_args` | `.pool_handle`, `.size`, `.va_out` |
| `mem_pool_alloc_async` | `gpu_mem_pool_alloc_async_args` | `.pool_handle`, `.size`, `.stream_id`, `.va_out`, `.fence_id_out` |
| `mem_pool_free_async` | `gpu_mem_pool_free_async_args` | `.va`, `.stream_id`, `.fence_id_out` |

### REQ-S3-3: cuStreamCapture Shim (self-contained)

The shim layer MUST expose `cuStreamBeginCapture`, `cuStreamEndCapture`, `cuStreamIsCapturing` as REAL_IMPL using a **self-contained state machine** (`async_task::umd::shim::CaptureTable`) — matching the cu_stream.cpp / cu_event.cpp pattern. The shim MUST NOT call GpuDriverClient.

#### Scenario: cuStreamBeginCapture with GLOBAL mode

- **GIVEN** a valid CUstream handle and current capture state is NONE
- **WHEN** `cuStreamBeginCapture(stream, CU_STREAM_CAPTURE_MODE_GLOBAL)` is called
- **THEN** the system MUST update `CaptureTable.state[stream] = 1` (ACTIVE)
- **AND** return `CUDA_SUCCESS`

#### Scenario: cuStreamBeginCapture with non-GLOBAL mode (F-1)

- **GIVEN** any CUstream handle
- **WHEN** `cuStreamBeginCapture(stream, CU_STREAM_CAPTURE_MODE_THREAD_LOCAL)` (or RELAXED) is called
- **THEN** the system MUST return `CUDA_ERROR_NOT_SUPPORTED` WITHOUT updating CaptureTable
- **AND** capture state MUST remain unchanged

#### Scenario: cuStreamBeginCapture when already ACTIVE → INVALID

- **GIVEN** `CaptureTable.state[stream] == 1` (ACTIVE)
- **WHEN** `cuStreamBeginCapture(stream, GLOBAL)` is called
- **THEN** the system MUST set `CaptureTable.state[stream] = 2` (INVALID)
- **AND** return `CUDA_ERROR_ILLEGAL_STATE`

#### Scenario: cuStreamEndCapture returns graph handle

- **GIVEN** capture state is ACTIVE
- **WHEN** `cuStreamEndCapture(stream, &graph)` is called
- **THEN** the system MUST set `CaptureTable.state[stream] = 0` (NONE)
- **AND** populate `*phGraph` with a fresh `next_graph_id` (atomic counter)
- **AND** return `CUDA_SUCCESS`

### REQ-S3-4: cuGraph Shim (self-contained, 11 funcs)

The shim layer MUST expose cuGraphCreate, cuGraphDestroy, cuGraphAddKernelNode, cuGraphAddMemcpyNode, cuGraphInstantiate, cuGraphLaunch, cuGraphExecDestroy, cuGraphNodeGetType, cuGraphNodeSetAttribute, cuGraphExecKernelNodeSetParams, cuGraphExecMemcpyNodeSetParams as REAL_IMPL using self-contained `GraphTable`.

#### Scenario: cuGraphCreate returns graph handle

- **GIVEN** a non-null `CUgraph* phGraph`
- **WHEN** `cuGraphCreate(phGraph, flags)` is called
- **THEN** the system MUST allocate a new graph ID via `next_graph_id.fetch_add(1)`
- **AND** populate `*phGraph = reinterpret_cast<CUgraph>(static_cast<uintptr_t>(id))`
- **AND** register `graph_meta[*phGraph] = id`
- **AND** return `CUDA_SUCCESS`

#### Scenario: cuGraphAddKernelNode with kernargs_bo_handle == 0 (F-3)

- **GIVEN** a valid graph handle and `CUDA_KERNEL_NODE_PARAMS` with `kernelParams == nullptr` (or kernargs at index 0 == 0)
- **WHEN** `cuGraphAddKernelNode(...)` is called
- **THEN** the system MUST pass `kernargs_bo_handle = 0` transparently (no validation)
- **AND** allocate a fresh node ID via `next_node_id.fetch_add(1)`
- **AND** append the node ID to `graph_nodes[graph_id]`
- **AND** return `CUDA_SUCCESS`

#### Scenario: cuGraphLaunch (PoC no-op)

- **GIVEN** a valid exec handle registered in `exec_meta`
- **WHEN** `cuGraphLaunch(hGraphExec, hStream)` is called
- **THEN** the system MUST validate the exec handle
- **AND** return `CUDA_SUCCESS` (Phase 3.1 PoC: no actual launch, no fence_id return)
- **NOTE**: Phase 4+ will extend this to call `GpuDriverClient::submit_graph` and return `fence_id ≥ 1<<32`

### REQ-S3-5: cuMemPool Shim (self-contained, 8 funcs, Option B)

The shim layer MUST expose cuMemPoolCreate, cuMemPoolDestroy, cuMemPoolAlloc, cuMemPoolFree, cuMemPoolAllocAsync, cuMemPoolFreeAsync, cuMemPoolSetAttribute, cuMemPoolGetAttribute, cuMemPoolTrim as REAL_IMPL using self-contained `MemPoolTable`.

#### Scenario: cuMemPoolCreate with vaSpaceHandle == 0 (B-2 Option B)

- **GIVEN** `CUmemPoolProps.vaSpaceHandle == 0` (H-1 sentinel)
- **WHEN** `cuMemPoolCreate(pool, &props)` is called
- **THEN** the system MUST return `CUDA_ERROR_INVALID_VALUE` WITHOUT allocating
- **AND** `MemPoolTable` MUST remain unchanged

#### Scenario: cuMemPoolCreate with valid vaSpaceHandle

- **GIVEN** `CUmemPoolProps.vaSpaceHandle != 0`
- **WHEN** `cuMemPoolCreate(pool, &props)` is called
- **THEN** the system MUST allocate a fresh pool ID via `next_pool_id.fetch_add(1)`
- **AND** register `pool_meta[*pool] = props.maxSize`
- **AND** return `CUDA_SUCCESS`

#### Scenario: cuMemPoolSetAttribute with invalid attr (F-2)

- **GIVEN** any pool handle and `attr` not in {RELEASE_THRESHOLD, REUSE_FOLLOW_EVENT_DEPENDENCIES}
- **WHEN** `cuMemPoolSetAttribute(pool, attr, value)` is called
- **THEN** the system MUST return `CUDA_ERROR_NOT_SUPPORTED`
- **AND** MUST NOT modify MemPoolTable

#### Scenario: cuMemPoolAllocAsync reuses sync impl (PoC)

- **GIVEN** a valid pool handle
- **WHEN** `cuMemPoolAllocAsync(dptr, size, pool, stream)` is called
- **THEN** the system MUST call `cuMemPoolAlloc(dptr, size, pool, nullptr)` internally
- **AND** return `CUDA_SUCCESS` (no fence_id in PoC; Phase 4 will add real fence_id)

### REQ-S3-6: Existing Stub Migration

The system MUST remove the following obsolete stubs from existing files:

| Existing stub | Current location | Action |
|---------------|------------------|--------|
| `cuStreamBeginCapture` | `src/umd/libcuda_shim/cu_stream.cpp:95-100` | DELETE |
| `cuStreamEndCapture` | `src/umd/libcuda_shim/cu_stream.cpp:102-106` | DELETE |
| `cuStreamGetCaptureInfo` | `src/umd/libcuda_shim/cu_stream.cpp:140-148` | MODIFY to call `cuStreamIsCapturing` |
| `cuGraphCreate` | `src/umd/libcuda_shim/cu_mem.cpp:258-261` | DELETE |

#### Scenario: cuStreamGetCaptureInfo migration to cuStreamIsCapturing

- **GIVEN** any CUstream handle (valid or null)
- **WHEN** `cuStreamGetCaptureInfo(stream, captureStatus, id)` is called after migration
- **THEN** the system MUST delegate to `cuStreamIsCapturing(stream, captureStatus)`
- **AND** if `id != nullptr`, set `*id = 0` (PoC: graph capture ID not yet tracked)
- **AND** return value semantics match `cuStreamIsCapturing`
- **BEHAVIOR CHANGE**: prior implementation always returned `CU_STREAM_CAPTURE_STATUS_NONE`; new implementation reflects actual `CaptureTable.state[stream]`.

### REQ-S3-7: E2E Test Coverage

The system MUST provide ≥75 new E2E test cases covering the 22 new shim functions:

| Test binary | Test cases | Coverage |
|-------------|-----------|----------|
| `test_cu_stream_capture` | ≥30 | State machine, GLOBAL mode, fence_id, multi-stream |
| `test_cu_graph` | ≥20 | Lifecycle, add_*, launch, exec, error paths |
| `test_cu_mem_pool` | ≥25 | Lifecycle, sync/async alloc, attributes, trim, Option B |
| **Total** | **≥75** | All 22 shim funcs + GpuDriverClient 15 forwarding + integration |

**Total shim functions**: **22** (3 cuStreamCapture + 11 cuGraph + 8 cuMemPool)
**Total GpuDriverClient overrides added**: **15**

#### Scenario: test_cu_stream_capture: GLOBAL capture cycle

- **GIVEN** a fresh CUstream
- **WHEN** test calls cuStreamBeginCapture(GLOBAL) → cuMemsetD32Async × 3 → cuStreamEndCapture
- **THEN** test verifies:
  - cuStreamIsCapturing returns ACTIVE during capture
  - cuStreamIsCapturing returns NONE after end
  - End returns non-null CUgraph
  - The graph has 3 recorded nodes (verified via cuGraphExecKernelNodeSetParams / introspection)

#### Scenario: test_cu_mem_pool: Option B VA boundary

- **GIVEN** a pool created with maxSize = 4MB
- **WHEN** test calls cuMemPoolAlloc with size = 5MB
- **THEN** test verifies:
  - cuMemPoolAlloc returns `CUDA_ERROR_OUT_OF_MEMORY`
  - Pool state is unchanged (no partial allocation)
  - Subsequent cuMemPoolAlloc(2MB) succeeds (proves capacity is still 4MB)

## MODIFIED Requirements

### REQ-IGPU-EXT-1 (Modified): 46-method IGpuDriver Contract

The IGpuDriver contract (TADR-301) MUST reflect 46 methods (31 + 15), all virtual with default no-op (NON-pure). Step 1 commit 21f71c9 satisfies this; Step 3 implements the 15 forwarding overrides in GpuDriverClient.

### REQ-SHIM-COUNT-1 (Modified): REAL_IMPL API count

The shim layer's REAL_IMPL API count MUST increase from 102 to ≥119 with Step 3 implementation:
- 11 cuGraph/cuStreamCapture APIs (commit C3+C4)
- 8 cuMemPool APIs (commit C5)

The 4 STUB sanity false-positives in docs-audit MUST be cleared (cu* API no longer STUB).

### REQ-DOCS-AUDIT-1 (Modified): docs-audit.sh --strict

The `tools/docs-audit.sh --strict` check MUST continue to pass (exit 0) after Step 3 implementation. No new false-positives MUST be introduced.

### REQ-SYNC-PLAN-1 (Modified): Cross-Repo Sync Plan

The `plans/sync-plan.md` MUST be updated to v2.3 reflecting:
- Step 3 status entries (5 workstreams)
- Step 4 trigger (submodule bump)
- Final regression timeline (target 2026-07-22)

## REMOVED Requirements

None — Step 3 is purely additive (with stub migration in §REQ-S3-6).

## Cross-References

- **TADR-301** (IGpuDriver 46-method contract): [../../../shared/adr/tadr-301-igpu-driver-contract.md](../../../shared/adr/tadr-301-igpu-driver-contract.md)
- **TADR-109** (IGpuDriver uniform scheduling, similar non-pure pattern): [../../../test-fixture/adr/tadr-109-igpu-driver-uniform-scheduling.md](../../../test-fixture/adr/tadr-109-igpu-driver-uniform-scheduling.md)
- **Step 1 openspec**: [../../2026-07-05-phase3-1-igpu-driver-extension/](../2026-07-05-phase3-1-igpu-driver-extension/)
- **Step 2 upstream (UsrLinuxEmu)**: [https://github.com/chisuhua/UsrLinuxEmu/pull/20](https://github.com/chisuhua/UsrLinuxEmu/pull/20)
- **Coordination PR**: [../../../superpowers/cross-repo-prs/2026-07-05-phase3-1-stream-mempool-coordination.md](../../../superpowers/cross-repo-prs/2026-07-05-phase3-1-stream-mempool-coordination.md)
- **UsrLinuxEmu accepted-resolution.md** (Fix-1~14): [../../../../../../UsrLinuxEmu/openspec/changes/2026-07-05-sim-stream-primitive-support/accepted-resolution.md](../../../../../../UsrLinuxEmu/openspec/changes/2026-07-05-sim-stream-primitive-support/accepted-resolution.md)
- **Existing shim pattern reference**: `src/umd/libcuda_shim/cu_stream.cpp`, `cu_event.cpp`, `cu_mem.cpp` (self-contained atomic + map + mutex)

## Decision Numbering

- **D-SC-5** Capture mode 限制（GLOBAL only）— implemented in shim layer (F-1)
- **D-SC-9** GpuQueueEmu API integration path — implemented in UsrLinuxEmu Step 2
- **D-SC-11** fence_id 范围划分 — implemented in sim (B-3)
- **D-SC-12** kernargs_bo_handle=0 语义 — implemented in shim + sim (F-3)
- **D-MP-1** Pool VA 范围采用 Option B — implemented in shim + sim (B-2)
- **D-S3-1** Self-contained shim pattern (NEW, Step 3) — atomic + map + mutex per file
- **D-S3-2** GpuDriverClient inline ioctl pattern (NEW, Step 3) — no `submit_ioctl()` helper
- **D-S3-3** Existing stub migration (NEW, Step 3) — remove 3 obsolete NOT_IMPLEMENTED stubs