# Spec: igpu-driver-extension

> **Capability**: `igpu-driver-phase3-extension` (TaskRunner internal)
> **Type**: New capability (interface extension)
> **Status**: PROPOSED
> **Date**: 2026-07-05
> **Cross-ref**: UsrLinuxEmu OpenSpec `2026-07-05-sim-stream-primitive-support/` (ACCEPTED)

## Purpose

Extend IGpuDriver interface from 31 to 46 methods to support Phase 3.1 (Stream Capture + CUDA Graph) and Phase 3.2 (Memory Pool). All new methods are virtual with default no-op implementations for backward compatibility.

## Requirements

### REQ-1: 10 Stream Capture + Graph Methods

**SHALL**: IGpuDriver SHALL expose 10 new methods for Phase 3.1 stream capture and graph management.

#### Scenario 1.1: stream_capture_status

**WHEN** the IGpuDriver subclass (e.g., CudaStub in stub mode) receives `stream_capture_status(stream_id, &status_out)`

**THEN** default implementation SHALL return `-1` (not implemented)

**AND** `*status_out` SHALL remain unchanged

#### Scenario 1.2: stream_capture_begin

**WHEN** `stream_capture_begin(stream_id, mode)` is called

**THEN** default implementation SHALL return `-1`

**AND** the implementation SHALL accept any `mode` value (validation happens in UsrLinuxEmu sim layer, not in IGpuDriver default)

#### Scenario 1.3: stream_capture_end

**WHEN** `stream_capture_end(stream_id, &graph_handle_out)` is called with non-null `graph_handle_out`

**THEN** default implementation SHALL return `-1`

**AND** `*graph_handle_out` SHALL remain unchanged

#### Scenario 1.4: graph_create

**WHEN** `graph_create(&graph_handle_out)` is called

**THEN** default implementation SHALL return `-1`

#### Scenario 1.5: graph_destroy

**WHEN** `graph_destroy(graph_handle)` is called

**THEN** default implementation SHALL return `-1`

#### Scenario 1.6: graph_add_kernel_node

**WHEN** `graph_add_kernel_node(graph_handle, kernel_index, grid, block, kernargs_bo_handle)` is called with `kernargs_bo_handle == 0`

**THEN** default implementation SHALL return `-1` (no validation needed; driver layer handles kernargs=0 case per UsrLinuxEmu F-3 decision)

#### Scenario 1.7: graph_add_memcpy_node

**WHEN** `graph_add_memcpy_node(graph_handle, src_va, dst_va, size, is_h2d)` is called

**THEN** default implementation SHALL return `-1`

#### Scenario 1.8: graph_instantiate

**WHEN** `graph_instantiate(graph_handle, &exec_handle_out)` is called

**THEN** default implementation SHALL return `-1`

#### Scenario 1.9: submit_graph

**WHEN** `submit_graph(graph_exec_handle, stream_id)` is called

**THEN** default implementation SHALL return `int64_t(-1)` (not implemented, returns negative errno)

**AND** the return type SHALL be `int64_t` (per UsrLinuxEmu B-3 + F-4 decision: fence_id range split)

#### Scenario 1.10: destroy_graph_exec

**WHEN** `destroy_graph_exec(graph_exec_handle)` is called

**THEN** default implementation SHALL return `-1`

### REQ-2: 5 Memory Pool Methods

**SHALL**: IGpuDriver SHALL expose 5 new methods for Phase 3.2 memory pool management.

#### Scenario 2.1: mem_pool_create

**WHEN** `mem_pool_create(va_space_handle, size, flags, &pool_handle_out)` is called

**THEN** default implementation SHALL return `-1`

**AND** the design SHALL reserve VA sub-range per Option B (UsrLinuxEmu B-2 decision) when overridden

#### Scenario 2.2: mem_pool_destroy

**WHEN** `mem_pool_destroy(pool_handle)` is called

**THEN** default implementation SHALL return `-1`

#### Scenario 2.3: mem_pool_alloc

**WHEN** `mem_pool_alloc(pool_handle, size, &va_out)` is called

**THEN** default implementation SHALL return `-1`

#### Scenario 2.4: mem_pool_alloc_async

**WHEN** `mem_pool_alloc_async(pool_handle, size, stream_id, &va_out)` is called

**THEN** default implementation SHALL return `int64_t(-1)`

**AND** the return type SHALL be `int64_t` (per UsrLinuxEmu B-3 + F-4 decision)

#### Scenario 2.5: mem_pool_free_async

**WHEN** `mem_pool_free_async(va, stream_id)` is called

**THEN** default implementation SHALL return `int64_t(-1)`

**AND** the return type SHALL be `int64_t` (per UsrLinuxEmu B-3 + F-4 decision)

### REQ-3: Backward Compatibility

**SHALL**: Existing IGpuDriver subclasses (CudaStub, MockGpuDriver) SHALL compile and run without modification after IGpuDriver extension.

#### Scenario 3.1: CudaStub compilation

**WHEN** the existing CudaStub class (`src/test_fixture/cuda_stub.cpp`) is compiled against the new 46-method IGpuDriver

**THEN** compilation SHALL succeed without errors

**AND** CudaStub's existing 31 method overrides SHALL remain functional

**AND** the 15 new methods SHALL inherit default no-op behavior from IGpuDriver base class

#### Scenario 3.2: MockGpuDriver compilation

**WHEN** the existing MockGpuDriver class (`tests/mock/mock_gpu_driver.cpp`) is compiled against the new 46-method IGpuDriver

**THEN** compilation SHALL succeed

#### Scenario 3.3: Existing tests

**WHEN** all existing 76+ tests (`test_cuda_scheduler` + `test_gpu_architecture` + `test_gpu_phase2` + `test_cuda_runtime_api` + `test_cuda_shim`) are run

**THEN** all tests SHALL pass

**AND** no new warnings SHALL appear in build output

### REQ-4: No Cross-Repo Dependencies

**SHALL**: The IGpuDriver extension SHALL NOT reference any UsrLinuxEmu GPU_IOCTL_* #define or other UsrLinuxEmu symbols.

#### Scenario 4.1: Self-contained header

**WHEN** `include/shared/igpu_driver.hpp` is compiled in isolation (without UsrLinuxEmu gpu_ioctl.h)

**THEN** compilation SHALL succeed (assuming existing prerequisites are met)

**AND** `grep "GPU_IOCTL" include/shared/igpu_driver.hpp` SHALL return no results

#### Scenario 4.2: Step 1 independence

**WHEN** Step 1 (this change) is committed and pushed to TaskRunner `main`

**THEN** UsrLinuxEmu Step 2 (sim + IOCTL merge) SHALL NOT be required for TaskRunner Step 1 to compile

### REQ-5: Decision Documentation

**SHALL**: The IGpuDriver extension SHALL explicitly document the UsrLinuxEmu Architecture Team decisions that influenced the design.

#### Scenario 5.1: B-3 fence_id documentation

**WHEN** the `submit_graph` method is documented in `igpu_driver.hpp`

**THEN** the doc comment SHALL reference UsrLinuxEmu B-3 decision (fence_id range split)

**AND** SHALL explain `int64_t` return type rationale

#### Scenario 5.2: F-1 capture mode documentation

**WHEN** the `stream_capture_begin` method is documented

**THEN** the doc comment SHALL reference UsrLinuxEmu F-1 decision (GLOBAL mode only)

#### Scenario 5.3: F-3 kernargs documentation

**WHEN** the `graph_add_kernel_node` method is documented

**THEN** the doc comment SHALL reference UsrLinuxEmu F-3 decision (`kernargs_bo_handle == 0` valid)

#### Scenario 5.4: B-2 pool VA documentation

**WHEN** the `mem_pool_create` method is documented

**THEN** the doc comment SHALL reference UsrLinuxEmu B-2 decision (Option B VA sub-range)

### REQ-6: Return Value Conventions

**SHALL**: The 3 fence_id-returning methods SHALL follow the int64_t convention per UsrLinuxEmu F-4.

#### Scenario 6.1: Convention

**WHEN** any of `submit_graph` / `mem_pool_alloc_async` / `mem_pool_free_async` is called

**THEN** the return value SHALL be interpreted as:
- `< 0` → negative errno
- `>= (1 << 32)` → valid fence_id
- `0` → success but no fence
- `-1` → not implemented (default no-op)

#### Scenario 6.2: No casts in shim layer

**WHEN** GpuDriverClient override (Step 3) implements these methods

**THEN** the implementation SHALL NOT cast `int64_t` return value to `int`

**AND** SHALL propagate negative errno verbatim to caller

## Cross-References

- **Related changes**:
  - UsrLinuxEmu `openspec/changes/2026-07-05-sim-stream-primitive-support/` (ACCEPTED)
  - TaskRunner `openspec/changes/2026-07-05-phase3-1-igpu-driver-extension/` (this change)

- **Decision docs**:
  - TaskRunner `docs/superpowers/specs/2026-07-02-phase3-stream-capture-design.md` (405 lines, contains B-1/B-3/F-1/F-3/F-4 decisions)
  - TaskRunner `docs/superpowers/specs/2026-07-02-phase3-mempool-design.md` (269 lines, contains B-2 decision)
  - TaskRunner `docs/superpowers/cross-repo-prs/2026-07-05-phase3-1-stream-mempool-coordination.md` (507 lines, §12 documents all 11 resolutions)

- **Related ADRs**:
  - [ADR-032 H-2.5 IGpuDriver 抽象层](https://github.com/chisuhua/TaskRunner) (UsrLinuxEmu side)
  - [TADR-301 IGpuDriver 31 方法契约](../../../shared/adr/tadr-301-igpu-driver-contract.md)
  - [TADR-109 H-3.5 生命周期扩展](../../../test-fixture/adr/tadr-109-igpu-driver-uniform-scheduling.md)