# shim-default-init-fallback Specification

## Purpose
TBD - created by archiving change g-gpu-client-meyers-singleton-fallback. Update Purpose after archive.
## Requirements
### Requirement: shim accessor helper returns default CudaStub when g_gpu_client is null

The `async_task::umd::shim::get_driver_or_default()` function MUST return
`g_gpu_client` when it is non-null, and MUST return a Meyers-singleton
`CudaStub` instance when `g_gpu_client` is null. The function MUST be marked
`inline` to avoid multiple-definition errors when included from multiple
translation units (cu_stream.cpp, cu_graph.cpp, cu_mem_pool.cpp).

The Meyers singleton is constructed on first call, never destroyed, and never
assigned to `g_gpu_client`.

#### Scenario: shim accessor returns g_gpu_client when set

- **WHEN** `g_gpu_client` is non-null (e.g., `g_gpu_client = &g_mock;` in test setup)
- **AND** `get_driver_or_default()` is called
- **THEN** the return value MUST equal `g_gpu_client`
- **AND** the Meyers singleton MUST NOT be constructed (no observable side effect)

#### Scenario: shim accessor returns Meyers-singleton CudaStub when g_gpu_client is null

- **WHEN** `g_gpu_client == nullptr`
- **AND** `get_driver_or_default()` is called for the first time
- **THEN** the function MUST construct a `CudaStub` instance internally (Meyers singleton)
- **AND** the function MUST return a valid non-null `IGpuDriver*` pointer
- **AND** `g_gpu_client` MUST remain null (fallback MUST NOT mutate the global)

#### Scenario: shim accessor returns same Meyers-singleton instance across calls

- **WHEN** `g_gpu_client == nullptr`
- **AND** `get_driver_or_default()` is called multiple times
- **THEN** every call MUST return the same `IGpuDriver*` pointer (Meyers singleton guarantee)
- **AND** the CudaStub state (e.g., `next_fence_id_`, `queue_map_`) MUST persist across calls

### Requirement: cuStreamSynchronize MUST use fallback accessor when g_gpu_client is null

`cuStreamSynchronize` MUST use `async_task::umd::shim::get_driver_or_default()`
(in `src/umd/libcuda_shim/cu_stream.cpp`) instead of directly dereferencing
`g_gpu_client`. The previous null-guard returning `CUDA_ERROR_NOT_INITIALIZED`
MUST be removed; the fallback accessor guarantees a non-null driver.

#### Scenario: cuStreamSynchronize returns SUCCESS via CudaStub fallback

- **WHEN** `g_gpu_client == nullptr` (no explicit setup in test)
- **AND** a `CUstream` has been created via `cuStreamCreate(&s, 0)`
- **AND** `cuStreamSynchronize(s)` is called
- **THEN** the function MUST return `CUDA_SUCCESS` (via CudaStub::wait_fence which returns success immediately)
- **AND** `g_gpu_client` MUST remain null

#### Scenario: cuStreamSynchronize still respects explicit g_gpu_client override

- **WHEN** `g_gpu_client = &g_mock;` is set explicitly in test setup
- **AND** `g_mock.set_canned_return("wait_fence", 1)` is configured
- **AND** `cuStreamSynchronize(stream)` is called
- **THEN** the function MUST invoke `g_mock->wait_fence(...)` (not the Meyers fallback)
- **AND** the function MUST return based on the mock's canned return value

### Requirement: cuGraphLaunch succeeds via fallback when g_gpu_client is null

The `cuGraphLaunch` shim function (in `src/umd/libcuda_shim/cu_graph.cpp`) MUST
use `async_task::umd::shim::get_driver_or_default()` instead of directly
dereferencing `g_gpu_client`. The previous null-guard returning
`CUDA_ERROR_NOT_INITIALIZED` MUST be removed.

#### Scenario: cuGraphLaunch returns SUCCESS via CudaStub fallback

- **WHEN** `g_gpu_client == nullptr` (no explicit setup in test)
- **AND** a `CUgraphExec` and `CUstream` have been created via cuGraph APIs
- **AND** `cuGraphLaunch(exec, stream)` is called
- **THEN** the function MUST return `CUDA_SUCCESS` (via CudaStub::submit_graph mock)
- **AND** `g_gpu_client` MUST remain null

### Requirement: cuMemPoolCreate MUST use fallback accessor when g_gpu_client is null

`cuMemPoolCreate` MUST use `async_task::umd::shim::get_driver_or_default()`
(in `src/umd/libcuda_shim/cu_mem_pool.cpp`) instead of directly dereferencing
`g_gpu_client`. The previous null-guard returning `CUDA_ERROR_NOT_INITIALIZED`
MUST be removed.

#### Scenario: cuMemPoolCreate returns SUCCESS via CudaStub fallback

- **WHEN** `g_gpu_client == nullptr` (no explicit setup in test)
- **AND** `cuMemPoolCreate(&pool, &props)` is called
- **THEN** the function MUST return `CUDA_SUCCESS` (via CudaStub's mock mem_pool method)
- **AND** `g_gpu_client` MUST remain null

### Requirement: init_gpu_client() MUST still create GpuDriverClient (no short-circuit regression)

`init_gpu_client()` MUST continue to create a `GpuDriverClient` instance and
assign it to `g_gpu_client` when called from `cli_main.cpp`. The Meyers
singleton fallback MUST NOT mutate `g_gpu_client`, so `init_gpu_client()`'s
`if (g_gpu_client) return 0;` short-circuit MUST NOT trigger when the fallback
has been used.

#### Scenario: CLI path creates GpuDriverClient (not CudaStub)

- **WHEN** `cli_main` calls `init_gpu_client()` and `g_gpu_client` is initially null
- **AND** the Meyers singleton fallback has been triggered by earlier shim calls in tests
- **THEN** `init_gpu_client()` MUST create a new `GpuDriverClient` and assign it to `g_gpu_client`
- **AND** `init_gpu_client()` MUST NOT return 0 due to the Meyers singleton (since `g_gpu_client` is still null at the time of `init_gpu_client()` call from CLI)
- **AND** subsequent CLI commands (cuda_alloc, cuda_memcpy, etc.) MUST use the `GpuDriverClient` (real `/dev/gpgpu0` ioctl), not the CudaStub mock

### Requirement: All shim files use the shared accessor (no local null guards)

After this change is applied, the following 3 shim files MUST include
`cuda_driver_accessor.hpp` and MUST use `get_driver_or_default()` instead of
any local null guard on `g_gpu_client`:

- `src/umd/libcuda_shim/cu_stream.cpp`
- `src/umd/libcuda_shim/cu_graph.cpp`
- `src/umd/libcuda_shim/cu_mem_pool.cpp`

This requirement is verified by `grep -L "cuda_driver_accessor.hpp" src/umd/libcuda_shim/{cu_stream,cu_graph,cu_mem_pool}.cpp` returning no results.

#### Scenario: All 3 shim files include the shared accessor

- **WHEN** `grep -l "cuda_driver_accessor.hpp" src/umd/libcuda_shim/cu_stream.cpp src/umd/libcuda_shim/cu_graph.cpp src/umd/libcuda_shim/cu_mem_pool.cpp` is executed
- **THEN** all 3 files MUST be listed (each contains the include)
- **AND** `grep -E "if \(!?g_gpu_client\)" src/umd/libcuda_shim/cu_stream.cpp src/umd/libcuda_shim/cu_graph.cpp src/umd/libcuda_shim/cu_mem_pool.cpp` MUST return no results (no local null guards)

### Requirement: shim functions return SUCCESS instead of NOT_INITIALIZED when g_gpu_client is null

`cuStreamSynchronize`, `cuGraphLaunch`, and `cuMemPoolCreate` MUST return
`CUDA_SUCCESS` (via the Meyers singleton fallback) when `g_gpu_client ==
nullptr`. This is a behavioral change from the previous behavior of returning
`CUDA_ERROR_NOT_INITIALIZED` in the same condition.

This is a behavioral change for callers that previously relied on
`NOT_INITIALIZED` to detect missing setup. The only such callers in the existing
codebase are the test fixtures themselves, which explicitly set `g_gpu_client`
before testing edge cases. New tests that want to test NOT_INITIALIZED semantics
must use a build flag (out of scope for this change) or explicitly null-out the
fallback accessor (also out of scope).

#### Scenario: Behavioral change documented in CHANGELOG

- **WHEN** this change is merged to main
- **THEN** the change entry in CHANGELOG.md MUST mention that `cuStreamSynchronize`, `cuGraphLaunch`, `cuMemPoolCreate` no longer return `CUDA_ERROR_NOT_INITIALIZED` when `g_gpu_client == nullptr`
- **AND** the entry MUST point to this spec for migration guidance

