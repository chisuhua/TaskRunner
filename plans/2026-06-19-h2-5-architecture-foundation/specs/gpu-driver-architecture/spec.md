# Capability: gpu-driver-architecture

> **Status**: ⚠️ DRAFT
> **Owner**: TaskRunner 侧
> **Prerequisite**: 无（基础能力）
> **Successor**: `gpu-phase2-management` (H-3) —— 依赖本 capability 提供 `IGpuDriver` 抽象
> **Upstream**: UsrLinuxEmu `GPU_IOCTL_*` ABI（`plugins/gpu_driver/shared/gpu_ioctl.h`）

本 capability 跟踪 TaskRunner 的 GPU 驱动**架构基础**：`IGpuDriver` 抽象层 + 两个实现（`GpuDriverClient` 真实 ioctl / `CudaStub` mock）+ `CudaScheduler` 依赖注入 + `MockGpuDriver` 测试夹具 + CLI 死调用修复。一旦所有 8 个 ADDED Requirements 满足，本 capability 可归档。

## ADDED Requirements

### Requirement: `GpuDriverClient implements IGpuDriver`

The `async_task::gpu::GpuDriverClient` class MUST publicly inherit from `async_task::gpu::IGpuDriver` and override all 28 pure virtual methods declared in `include/igpu_driver.hpp` (lines 55-307). The 4 buffer-object methods MUST match the interface signatures after D6/D7/D8 reconciliation.

> **Note**: D6/D7/D8 reconciliation aligns `GpuDriverClient` signatures to the abstract interface (lines 146-169): `alloc_bo(size, flags) → u64`, `free_bo(u64 handle) → int`, `map_bo(handle, size) → void*`.

#### Scenario: `GpuDriverClient` compiles as `IGpuDriver` subclass

- **WHEN** `include/gpu_driver_client.h` declares `class GpuDriverClient : public IGpuDriver`
- **THEN** the compiler accepts the declaration (no missing overrides reported)
- **AND** all 28 virtual methods have explicit `override` markers
- **AND** a `static_assert(std::is_base_of_v<IGpuDriver, GpuDriverClient>)` test passes

#### Scenario: BO method signatures match IGpuDriver (D6/D7/D8)

- **WHEN** a caller invokes `GpuDriverClient::alloc_bo(uint64_t size, uint32_t flags)`
- **THEN** the method returns `uint64_t` (bo_handle, >= 1 on success, 0 on failure)
- **AND** `domain` is folded into `flags` (single combined bitfield argument)
- **WHEN** a caller invokes `GpuDriverClient::free_bo(uint64_t bo_handle)`
- **THEN** the method accepts `uint64_t` (widened from u32)
- **WHEN** a caller invokes `GpuDriverClient::map_bo(uint64_t bo_handle, uint64_t size)`
- **THEN** the method returns `void*` (CPU virtual address, nullptr on failure)
- **AND** no output `gpu_va` parameter is needed (replaced by return value)

#### Scenario: 5 H-3 Phase 2 placeholder methods throw runtime_error

- **WHEN** a caller invokes `create_va_space` / `destroy_va_space` / `register_gpu` / `create_queue` / `destroy_queue` on `GpuDriverClient`
- **THEN** each method throws `std::runtime_error("not implemented; see H-3")`
- **AND** no real ioctl is invoked
- **AND** the compiler still treats them as valid `override` (method exists, even if throws)

### Requirement: `CudaStub` migrates to `async_task::gpu` namespace + implements `IGpuDriver`

The `async_task::gpu::CudaStub` class (migrated from `taskrunner::CudaStub` per D9) MUST publicly inherit from `async_task::gpu::IGpuDriver` and override all 28 pure virtual methods. For backward compatibility, a 1-release `taskrunner::CudaStub` type alias MUST be provided.

> **Note**: D9 rejects the adapter pattern (which would wrap `taskrunner::CudaStub` in an `async_task::gpu::CudaStubAdapter`) in favor of direct namespace migration. This avoids runtime indirection and aligns all related types under one namespace.

#### Scenario: `CudaStub` is defined in `async_task::gpu`

- **WHEN** `include/cuda_stub.hpp` is parsed
- **THEN** `CudaStub` is declared in `namespace async_task::gpu`
- **AND** the class inherits from `IGpuDriver`
- **AND** the existing CUDA Driver API methods (`initialize`, `mem_alloc`, `memcpy_h2d`, `launch_kernel`, etc.) are preserved with their original signatures

#### Scenario: All 28 IGpuDriver methods overridden with mock semantics

- **WHEN** a caller invokes any `IGpuDriver` method on a `CudaStub` instance
- **THEN** the method is routed to the mock implementation
- **AND** `alloc_bo(size, flags)` returns a monotonic `uint64_t` (from internal counter, no real allocation)
- **AND** `map_bo(handle, size)` returns a `malloc(size)`-allocated host pointer (mock CPU mapping)
- **AND** `submit_memcpy` / `submit_launch` return a monotonic `int64_t` fence_id (no real submit)
- **AND** no real ioctl or GPU access occurs

#### Scenario: Backward-compatibility alias for `taskrunner::CudaStub`

- **WHEN** existing code references `taskrunner::CudaStub` (old namespace)
- **THEN** the compiler resolves it via `using CudaStub = async_task::gpu::CudaStub;` alias
- **AND** the resolved type is identical to `async_task::gpu::CudaStub` (same class)
- **AND** existing test code (`tests/test_cuda_scheduler.cpp`) compiles without modification

### Requirement: `CudaScheduler` accepts `IGpuDriver*` (DI)

The `taskrunner::CudaScheduler` class MUST accept an `async_task::gpu::IGpuDriver*` constructor parameter. When the parameter is `nullptr`, the scheduler MUST auto-create a `CudaStub` instance and own it. The internal member MUST be `IGpuDriver* driver_` (replacing `CudaStub* stub_`).

> **Note**: D10 maintains backward compatibility via two mechanisms: (1) `nullptr` parameter auto-creates `CudaStub`, (2) `CudaStub*` arguments implicitly convert to `IGpuDriver*` (since `CudaStub` now implements `IGpuDriver`).

#### Scenario: `CudaScheduler(IGpuDriver*)` with explicit injection

- **WHEN** a test constructs `CudaScheduler scheduler(&mock_gpu_driver)`
- **THEN** `scheduler.driver_` points to `mock_gpu_driver`
- **AND** `scheduler.owns_driver_` is `false` (does not delete on destruction)
- **AND** all subsequent `scheduler` calls route through `mock_gpu_driver` (verified via mock history)

#### Scenario: `CudaScheduler()` auto-creates `CudaStub`

- **WHEN** a caller constructs `CudaScheduler scheduler;` with no arguments
- **THEN** `scheduler.driver_` points to a newly-allocated `CudaStub` instance
- **AND** `scheduler.owns_driver_` is `true` (deletes on destruction)
- **AND** the 8 existing `test_cuda_scheduler` test cases continue passing without modification

#### Scenario: `CudaScheduler(CudaStub*)` backward compatibility

- **WHEN** legacy code passes a `CudaStub*` to the `CudaScheduler` constructor
- **THEN** the compiler implicitly converts `CudaStub*` to `IGpuDriver*`
- **AND** the scheduler uses the supplied `CudaStub` (does not auto-create)
- **AND** `owns_driver_` is `false` (caller still owns the stub)

### Requirement: `MockGpuDriver` test fixture available

A `MockGpuDriver` class MUST be provided in `tests/mock_gpu_driver.hpp`, implementing all 28 `IGpuDriver` methods with recording and canned-value semantics. The fixture MUST support injecting errors for error-path tests.

> **Note**: This is the test infrastructure that H-3 depends on. Without `MockGpuDriver`, H-3 cannot test new wrapper methods without triggering real `/dev/gpgpu0` ioctl.

#### Scenario: `MockGpuDriver` records every call

- **WHEN** a test calls `mock.alloc_bo(4096, 0)` and then `mock.free_bo(handle)`
- **THEN** `mock.history()` contains 2 entries:
  - `{method="alloc_bo", args_u64=[4096], args_u32=[0]}`
  - `{method="free_bo", args_u64=[handle]}`
- **AND** tests can assert exact call sequences and arguments

#### Scenario: `MockGpuDriver` supports error injection

- **WHEN** a test calls `mock.inject_alloc_bo_error(true)` then invokes `mock.alloc_bo(4096, 0)`
- **THEN** the method returns `0` (failure sentinel) without performing real work
- **AND** the call is still recorded in `mock.history()` (with `injected_error=true`)
- **AND** all 28 methods have a corresponding `inject_<method>_error(bool)` helper

#### Scenario: `MockGpuDriver` injectable into `CudaScheduler`

- **WHEN** a test creates `MockGpuDriver mock;` and `CudaScheduler s(&mock);`
- **THEN** all scheduler operations route through `mock`
- **AND** switching to `CudaScheduler s(&real_gpu_client);` (real) yields identical scheduler behavior with real ioctl (verifiable in integration tests)

### Requirement: CLI dead-call fix (`init_gpu_client()` invoked)

The `src/cli_main.cpp` `main()` function MUST invoke `init_gpu_client()` at startup (after argv parsing, before `cmd_buffer_v2_main`) and `shutdown_gpu_client()` at exit. When `init_gpu_client()` fails, the CLI MUST print a warning and continue (allowing `--test` mode to run without real GPU).

> **Note**: D11 closes the H-1-era gap where `g_gpu_client` was always `nullptr` at runtime because `init_gpu_client()` was never called. Without D11, the 4 CLI commands (`cuda_alloc` / `cuda_memcpy` / `cuda_launch` / `cuda_wait`) would crash on null dereference.

#### Scenario: `init_gpu_client()` is invoked at startup

- **WHEN** `./build/taskrunner cuda_alloc 4096` is run
- **THEN** `init_gpu_client()` is called before `cmd_buffer_v2_main`
- **AND** `g_gpu_client` is non-null when `cuda_alloc` runs
- **AND** the CLI reaches `GpuDriverClient` real ioctl path (not CudaStub mock)

#### Scenario: `shutdown_gpu_client()` is invoked at exit

- **WHEN** the CLI exits normally (or via `return ret`)
- **THEN** `shutdown_gpu_client()` is called
- **AND** the file descriptor in `g_gpu_client` is closed
- **AND** `g_gpu_client` is set to `nullptr` (no dangling pointer)

#### Scenario: `init_gpu_client()` failure does not abort CLI

- **WHEN** `init_gpu_client()` fails (e.g., `/dev/gpgpu0` not present in test mode)
- **THEN** the CLI prints "Warning: Failed to init GPU client (running in stub mode)" to `std::cerr`
- **AND** `cmd_buffer_v2_main` is still invoked (CLI proceeds in degraded mode)
- **AND** `./build/taskrunner --test` continues to work (test mode does not need real GPU)

### Requirement: Backward compatibility with H-1 callers

After H-2.5 changes, all existing call sites (including H-1's `setCurrentVASpace()` / `getCurrentVASpace()`) MUST compile and behave identically. New callers SHOULD use the snake_case `set_current_va_space()` / `get_current_va_space()` from `IGpuDriver`.

> **Note**: H-1's CamelCase methods (`setCurrentVASpace` / `getCurrentVASpace`) are retained as 1-release deprecated aliases that forward to the new snake_case methods. The H-1 `current_va_space_handle_` field in `GpuDriverClient` is preserved as a transmission cache (not removed).

#### Scenario: H-1 CamelCase alias forwards to snake_case

- **WHEN** legacy code calls `gpu_client.setCurrentVASpace(42)`
- **THEN** the alias forwards to `set_current_va_space(42)`
- **AND** `current_va_space_handle_` is set to `42`
- **AND** subsequent `submit_batch()` populates `args.va_space_handle = 42`

#### Scenario: Legacy callers compile without modification

- **WHEN** `src/cuda_scheduler.cpp` uses `gpu.setCurrentVASpace(handle)` (H-1 pattern)
- **THEN** the code compiles unchanged
- **AND** behavior matches H-1 closeout state (handle is plumbed through to `submit_batch`)
- **AND** the H-1 `submit_batch` validation logic continues to work (or skip via sentinel when handle == 0)

### Requirement: Existing test suite still passes (no regression)

The 8 existing `test_cuda_scheduler` test cases MUST continue passing after H-2.5 implementation, without modification to their source code.

> **Note**: Backward compatibility via D9 namespace alias + D10 auto-create fallback ensures zero-impact migration for existing tests.

#### Scenario: 8/8 existing test cases pass

- **WHEN** `./build/test_cuda_scheduler` is run after H-2.5 implementation
- **THEN** the test report shows 8/8 cases passing
- **AND** no test source file (`tests/test_cuda_scheduler.cpp`) was modified
- **AND** no test source file required re-compilation with new headers

#### Scenario: New H-2.5 tests added alongside existing

- **WHEN** `tests/test_gpu_architecture.cpp` is added with 8+ new test cases
- **THEN** `ctest` runs both `test_cuda_scheduler` (8 cases) and `test_gpu_architecture` (8+ cases)
- **AND** total tests are 16+ cases, all passing
- **AND** `CMakeLists.txt` registers both test targets

### Requirement: Cross-repo sync via H-1 closeout pattern

After H-2.5 implementation completes in the TaskRunner submodule, the cross-repo sync MUST follow the H-1 closeout pattern: direct commit (if push access) or pointer-only (if not), archive git tracking via `git add`, and 1 combined commit per related-task group.

> **Note**: D1/D3/D5 of H-1 closeout are reused verbatim. The submodule pointer MUST be updated exactly ONCE (not mid-implementation) to avoid partial state.

#### Scenario: Submodule pointer updated ONCE at implementation-complete

- **WHEN** TaskRunner-side implementation completes
- **THEN** a single UsrLinuxEmu commit updates `external/TaskRunner` gitlink to the new commit
- **AND** no mid-implementation submodule pointer updates occur
- **AND** the commit message references the openspec change (`h2-5-architecture-foundation`)

#### Scenario: openspec change archived with git tracking

- **WHEN** implementation and verification complete
- **THEN** `openspec archive h2-5-architecture-foundation` moves the change to `openspec/changes/archive/2026-06-19-h2-5-architecture-foundation/`
- **AND** the archive directory is added to git via `git add openspec/changes/archive/2026-06-19-h2-5-architecture-foundation/`
- **AND** the archive contains 6 files: README.md, .openspec.yaml, proposal.md, design.md, tasks.md, specs/gpu-driver-architecture/spec.md
- **AND** `openspec list` reports "No active changes"

#### Scenario: 1 combined commit per related-task group (D5 of H-1 closeout)

- **WHEN** the H-2.5 commit is made in the TaskRunner submodule
- **THEN** all related changes (GpuDriverClient : IGpuDriver + CudaStub namespace migration + CudaScheduler DI + MockGpuDriver + CLI fix) are combined into ONE commit
- **AND** the commit message names all 5 changes concisely (e.g., `feat(igpu): IGpuDriver abstraction + 2 impls + DI + Mock + CLI fix (H-2.5)`)
- **AND** git bisect can identify H-2.5 implementation with a single commit hash

## MODIFIED Requirements

_None — H-2.5 is purely additive at the capability level. The capability `gpu-driver-architecture` is NEW; no existing capability is modified._

## REMOVED Requirements

_None._

## Cross-references

- **H-3** (successor): `gpu-phase2-management` capability — depends on this capability's `IGpuDriver` interface and `MockGpuDriver` test fixture
- **H-1**: `gpu-pushbuffer-validation` capability — provides the VA Space validation logic that H-2.5's `set_current_va_space()` plumbs through
- **H-1 closeout**: `gpu-pushbuffer-validation-deployment` capability — provides the cross-repo sync pattern (D1/D3/D5) reused here
- **IGpuDriver 已起草**: `include/igpu_driver.hpp` (2026-06-22, 311 lines, 28 pure virtual methods)
- **Upstream ABI**: `UsrLinuxEmu/plugins/gpu_driver/shared/gpu_ioctl.h` (canonical source)
- **SSOT**: `docs/02_architecture/post-refactor-architecture.md` §1.3 (v0.1.5 待加)
- **DEPRECATED H-2** (history): `plans/2026-06-19-h2-phase2-openspec-skeleton/` — split source of this change