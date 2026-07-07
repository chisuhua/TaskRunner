---
SCOPE: umd-evolution + shared
STATUS: PROPOSED → REVISED 2026-07-07（方案 B：跨仓新增 `GPU_IOCTL_MEM_POOL_EXPORT`）
DATE: 2026-07-07
CHANGE: phase3-real-impl-bridge
RELATED_PROPOSAL: ../proposal.md
RELATED_DESIGN: ../design.md
RELATED_UPSTREAM: [UsrLinuxEmu PR #20](https://github.com/chisuhua/UsrLinuxEmu/pull/20) (Step 2, MERGED 2026-07-06, provides 8 IOCTL handlers 0x60-0x67)
RELATED_UPSTREAM_NEW: [UsrLinuxEmu PR phase4-mempool-export-ioctl](https://github.com/chisuhua/UsrLinuxEmu/pull/XXX) (Phase 4, PROPOSED, adds GPU_IOCTL_MEM_POOL_EXPORT 0x68)
RELATED_DOWNSTREAM: [TaskRunner PR #7](https://github.com/chisuhua/TaskRunner/pull/7) (Step 3, MERGED 2026-07-07, provides shim PoC + GpuDriverClient forwarding)
RELATED_DOWNSTREAM_NEW: [TaskRunner PR phase3-real-impl-bridge-extended](https://github.com/chisuhua/TaskRunner/pull/XXX) (Phase 4, PROPOSED)
RELATED_ADR: [tadr-301-igpu-driver-contract](../../../docs/shared/adr/tadr-301-igpu-driver-contract.md) (IGpuDriver 46 methods)
RELATED_ADR_NEW: [tadr-302-mempool-export-shareable](../../../docs/shared/adr/tadr-302-mempool-export-shareable.md) (IGpuDriver 47th method, Phase 4)
---

# Capability: phase3-real-impl-bridge

> Bridge Phase 3.1+3.2 PoC shim APIs to real GpuDriverClient IOCTL calls. Specifically:
> - `cuGraphLaunch`: PoC no-op → real `submit_graph` IOCTL (0x58)
> - `cuMemPoolAlloc`: PoC synthetic VA → real `mem_pool_alloc` IOCTL (0x62)
> - `cuMemPoolAllocAsync` / `cuMemPoolFreeAsync`: PoC sync-reuse → real async fence return (0x63/0x64)
> - `cuMemPoolExportToShareableHandle`: PoC no-op → real FD export (**NEW IOCTL 0x68**, Phase 4 跨仓新增)

## ADDED Requirements

### Requirement: cuGraphLaunch MUST dispatch to GpuDriverClient::submit_graph

The shim layer's `cuGraphLaunch(CUgraphExec hGraphExec, CUstream hStream)` MUST dispatch to `g_gpu_client->submit_graph(hGraphExec, stream_id)` (IOCTL 0x59) when `g_gpu_client != nullptr`. When `g_gpu_client == nullptr`, the function MUST return `CUDA_ERROR_NOT_INITIALIZED` (700) and log to stderr.

#### Scenario: cuGraphLaunch with valid exec and g_gpu_client set

- **GIVEN** a `CUgraphExec` handle `exec` from a prior `cuGraphInstantiate` call
- **AND** `g_gpu_client` is initialized and bound to a `GpuDriverClient` instance
- **WHEN** `cuGraphLaunch(exec, stream)` is called
- **THEN** the shim MUST call `g_gpu_client->submit_graph(exec, stream_id)` exactly once
- **AND** MUST record the returned `int64_t fence_id` to `LaunchTrace[exec]`
- **AND** MUST return `CUDA_SUCCESS` if `submit_graph` returned ≥ 0

#### Scenario: cuGraphLaunch with g_gpu_client nullptr

- **GIVEN** `g_gpu_client == nullptr` (e.g., stub test mode without driver binding)
- **WHEN** `cuGraphLaunch(exec, stream)` is called
- **THEN** the shim MUST return `CUDA_ERROR_NOT_INITIALIZED` (700)
- **AND** MUST log to stderr: `[cu_graph] cuGraphLaunch: g_gpu_client not initialized`

#### Scenario: cuGraphLaunch with NULL exec handle

- **WHEN** `cuGraphLaunch(nullptr, stream)` is called
- **THEN** the shim MUST return `CUDA_ERROR_INVALID_VALUE` (1) BEFORE attempting any GpuDriverClient call

#### Scenario: cuGraphLaunch propagates driver error

- **GIVEN** `g_gpu_client->submit_graph` returns `-1` (e.g., device closed)
- **WHEN** `cuGraphLaunch(exec, stream)` is called
- **THEN** the shim MUST return `CUDA_ERROR_UNKNOWN` (999) (or appropriate mapping of `-1`)
- **AND** MUST NOT record fence_id to LaunchTrace

### Requirement: cuMemPoolAlloc MUST dispatch to GpuDriverClient::mem_pool_alloc

The shim's `cuMemPoolAlloc(CUmemPoolPtr* ptr, size_t size, CUmemPool pool, CUmemPoolProps* props)` MUST dispatch to `g_gpu_client->mem_pool_alloc(pool_handle, size, &va_out)` (IOCTL 0x62) when `g_gpu_client != nullptr`. The returned `va_out` MUST be a real, dereferenceable GPU virtual address from the simulator.

#### Scenario: cuMemPoolAlloc with valid pool and size

- **GIVEN** a `CUmemPool` handle created by prior `cuMemPoolCreate`
- **AND** `g_gpu_client` is initialized
- **WHEN** `cuMemPoolAlloc(&ptr, 4096, pool, nullptr)` is called
- **THEN** the shim MUST call `g_gpu_client->mem_pool_alloc(pool, 4096, &va_out)` exactly once
- **AND** MUST write `*ptr = va_out` (real VA, not synthetic counter)
- **AND** MUST return `CUDA_SUCCESS` on success

#### Scenario: cuMemPoolAlloc with NULL ptr or zero size

- **WHEN** `cuMemPoolAlloc(nullptr, ...)` or `cuMemPoolAlloc(&ptr, 0, ...)` is called
- **THEN** the shim MUST return `CUDA_ERROR_INVALID_VALUE` BEFORE calling GpuDriverClient

#### Scenario: cuMemPoolAlloc propagates driver error

- **GIVEN** `g_gpu_client->mem_pool_alloc` returns -1 (e.g., pool exhausted)
- **WHEN** `cuMemPoolAlloc(&ptr, size, pool, nullptr)` is called
- **THEN** the shim MUST return appropriate CUDA error (NOT `CUDA_SUCCESS`)
- **AND** MUST leave `*ptr` unmodified

### Requirement: cuMemPoolAllocAsync MUST return sim fence_id

The shim MUST expose `cuMemPoolAllocAsync(CUmemPoolPtr* ptr, size_t size, CUmemPool pool, CUstream stream, CUmemPoolProps* props)` that calls `g_gpu_client->mem_pool_alloc_async(pool, size, stream_id, &va_out)` (IOCTL 0x63). The function MUST return `CUDA_SUCCESS` on submission, with the VA written to `*ptr` (dereferenceable after fence completion).

#### Scenario: cuMemPoolAllocAsync with valid inputs

- **GIVEN** valid pool handle and `g_gpu_client` set
- **WHEN** `cuMemPoolAllocAsync(&ptr, size, pool, stream, nullptr)` is called
- **THEN** the shim MUST call `g_gpu_client->mem_pool_alloc_async(pool, size, stream_id, &va_out)` exactly once
- **AND** MUST write `*ptr = va_out`
- **AND** MUST return `CUDA_SUCCESS`

#### Scenario: cuMemPoolAllocAsync with g_gpu_client nullptr

- **WHEN** `cuMemPoolAllocAsync(...)` is called with `g_gpu_client == nullptr`
- **THEN** MUST return `CUDA_ERROR_NOT_INITIALIZED`

### Requirement: cuMemPoolFreeAsync MUST return sim fence_id

The shim MUST expose `cuMemPoolFreeAsync(CUmemPoolPtr ptr, CUstream stream, CUmemPool pool)` that calls `g_gpu_client->mem_pool_free_async(va, stream_id)` (IOCTL 0x65). The function MUST return `CUDA_SUCCESS` on submission.

#### Scenario: cuMemPoolFreeAsync with valid VA

- **GIVEN** valid VA from prior `cuMemPoolAlloc`
- **AND** `g_gpu_client` set
- **WHEN** `cuMemPoolFreeAsync(ptr, stream, pool)` is called
- **THEN** the shim MUST call `g_gpu_client->mem_pool_free_async(va, stream_id)` exactly once
- **AND** MUST return `CUDA_SUCCESS`

#### Scenario: cuMemPoolFreeAsync with NULL ptr

- **WHEN** `cuMemPoolFreeAsync(nullptr, ...)` is called
- **THEN** MUST return `CUDA_ERROR_INVALID_VALUE` BEFORE GpuDriverClient call

### Requirement: cuMemPoolExportToShareableHandle MUST export real FD (REVISED 跨仓新增 IOCTL)

The shim's `cuMemPoolExportToShareableHandle(void* shareableHandle, CUmemPool pool, CUmemPoolHandleType handleType, unsigned int flags)` MUST dispatch to `g_gpu_client->mem_pool_export_shareable(pool, handle_type, flags, &fd_out)` (**新增 IGpuDriver 第 47 方法 → GPU_IOCTL_MEM_POOL_EXPORT IOCTL 0x68**) when `g_gpu_client != nullptr`. On success, the function MUST write a real file descriptor (POSIX FD) to `*shareableHandle`.

**Cross-repo dependency**: This requirement depends on UsrLinuxEmu 端新增的 `GPU_IOCTL_MEM_POOL_EXPORT` (IOCTL 0x68) IOCTL + handler。该 IOCTL 在 Phase 4 由 `UsrLinuxEmu PR phase4-mempool-export-ioctl` 引入（参考 `docs/00_adr/adr-XXX-mem-pool-export-ioctl.md`）。

#### Scenario: cuMemPoolExportToShareableHandle with POSIX_FD type

- **GIVEN** valid pool handle and `handleType = CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR`
- **AND** `g_gpu_client` set
- **WHEN** `cuMemPoolExportToShareableHandle(&fd_out, pool, CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR, 0)` is called
- **THEN** the shim MUST call `g_gpu_client->mem_pool_export_shareable(pool, type, 0, &fd_out)` exactly once
- **AND** MUST write a valid FD (≥ 0) to `*shareableHandle`
- **AND** MUST return `CUDA_SUCCESS`
- **AND** the returned FD MUST be `O_CLOEXEC` (so it doesn't leak across exec)

#### Scenario: cuMemPoolExportToShareableHandle with NULL handle or NULL pool

- **WHEN** `cuMemPoolExportToShareableHandle(nullptr, ...)` or `cuMemPoolExportToShareableHandle(h, nullptr, ...)` is called
- **THEN** MUST return `CUDA_ERROR_INVALID_VALUE` BEFORE GpuDriverClient call

#### Scenario: cuMemPoolExportToShareableHandle with unsupported handle type

- **GIVEN** `handleType != CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR` (e.g., `CU_MEM_HANDLE_TYPE_WIN32`, `CU_MEM_HANDLE_TYPE_FABRIC`)
- **WHEN** `cuMemPoolExportToShareableHandle(...)` is called
- **THEN** MUST return `CUDA_ERROR_NOT_SUPPORTED` (801) (Phase 5+ feature)

#### Scenario: cuMemPoolExportToShareableHandle with mock returning synthetic FD

- **GIVEN** `mock_instance.mem_pool_export_shareable` sets `*fd_out = 42` (synthetic FD for testing)
- **WHEN** `cuMemPoolExportToShareableHandle(&fd_out, pool, CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR, 0)` is called
- **THEN** `fd_out` MUST equal `42` (mock-provided FD)
- **AND** `mock_instance.mem_pool_export_shareable` call count MUST be 1

#### Scenario: cuMemPoolExportToShareableHandle propagates sim error

- **GIVEN** `g_gpu_client->mem_pool_export_shareable` returns `-1` (e.g., sim unable to allocate pipe FD)
- **WHEN** `cuMemPoolExportToShareableHandle(&fd_out, pool, ...)` is called
- **THEN** MUST return `CUDA_ERROR_UNKNOWN` (999)
- **AND** MUST leave `*fd_out` unmodified

### Requirement: Nullptr fallback MUST return CUDA_ERROR_NOT_INITIALIZED

When `g_gpu_client == nullptr`, all 5 REAL-bridged APIs MUST return `CUDA_ERROR_NOT_INITIALIZED` (700) and log to stderr. This includes `cuGraphLaunch`, `cuMemPoolAlloc`, `cuMemPoolAllocAsync`, `cuMemPoolFreeAsync`, `cuMemPoolExportToShareableHandle`.

#### Scenario: All REAL-bridged APIs return NOT_INITIALIZED when g_gpu_client is null

- **GIVEN** `g_gpu_client == nullptr`
- **WHEN** any of the 5 REAL-bridged APIs is called with valid arguments
- **THEN** MUST return `CUDA_ERROR_NOT_INITIALIZED` (700)
- **AND** MUST log a distinct message to stderr identifying the API

### Requirement: MockGpuDriver test injection MUST verify IOCTL calls

Tests MUST be able to inject a `MockGpuDriver` instance into `g_gpu_client` to verify shim → GpuDriverClient call patterns (call count, argument values, return propagation).

#### Scenario: MockGpuDriver verifies submit_graph is called once

- **GIVEN** a test that sets `g_gpu_client = &mock_instance`
- **AND** mock_instance.submit_graph returns `0x100000001` (sim fence_id)
- **WHEN** `cuGraphLaunch(exec, stream)` is called
- **THEN** mock_instance.submit_graph call count MUST be 1
- **AND** mock_instance.submit_graph MUST receive the original `exec` and `stream_id`

#### Scenario: MockGpuDriver returns synthetic VA for mem_pool_alloc

- **GIVEN** mock_instance.mem_pool_alloc returns `va_out = 0x200000`
- **WHEN** `cuMemPoolAlloc(&ptr, 4096, pool, nullptr)` is called
- **THEN** `*ptr` MUST equal `0x200000` (mock-provided VA, not monotonic counter)