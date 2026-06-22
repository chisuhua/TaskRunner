# Capability: gpu-phase2-management

> **Status**: ⚠️ DRAFT
> **Owner**: TaskRunner 侧
> **Prerequisite**: `gpu-driver-architecture` (H-2.5) — `IGpuDriver` 抽象接口
> **Upstream**: UsrLinuxEmu ADR-024 Phase 2 (Accepted v1)

本 capability 跟踪 TaskRunner `IGpuDriver` 的 5 个 Phase 2 ioctl wrapper 方法。一旦所有 9 个 ADDED Requirements 满足，本 capability 可归档。

## ADDED Requirements

### Requirement: `IGpuDriver::create_va_space(flags) → u64`

The `IGpuDriver` class MUST provide a method that calls `GPU_IOCTL_CREATE_VA_SPACE` (`gpu_ioctl.h` line 166) and returns the resulting VA Space handle as a `uint64_t`.

> **Note**: Method is part of `IGpuDriver` (H-2.5 abstract interface), implemented by `GpuDriverClient` (real ioctl) and `CudaStub` (mock). D1 decision: caller owns the returned handle; `GpuDriverClient` does NOT auto-store into `current_va_space_handle_`.

#### Scenario: Successful VA Space creation

- **WHEN** a caller invokes `create_va_space(flags)` on an open `IGpuDriver` implementation
- **AND** the ioctl succeeds (or mock returns canned non-zero value)
- **THEN** a non-zero `uint64_t` value (>= 1) is returned

#### Scenario: ioctl failure

- **WHEN** a caller invokes `create_va_space(flags)`
- **AND** the ioctl returns an error (e.g., no device, insufficient privileges)
- **THEN** the method returns `0`
- **AND** an error message is logged to `std::cerr` including `errno`

#### Scenario: Driver not open (is_open guard)

- **WHEN** a caller invokes `create_va_space(flags)` on an `IGpuDriver` where `is_open()` is `false`
- **THEN** the method returns `0` without invoking ioctl

### Requirement: `IGpuDriver::destroy_va_space(handle) → int`

The `IGpuDriver` class MUST provide a method that calls `GPU_IOCTL_DESTROY_VA_SPACE` (`gpu_ioctl.h` line 177) for a given VA Space handle.

#### Scenario: Successful VA Space destruction

- **WHEN** a caller invokes `destroy_va_space(handle)` with a valid `handle != 0`
- **AND** the ioctl succeeds
- **THEN** `0` is returned

#### Scenario: ioctl failure (VA Space still has attached queues)

- **WHEN** a caller invokes `destroy_va_space(handle)` on a valid handle
- **AND** the ioctl returns an error (e.g., `-EBUSY` because queues are still attached)
- **THEN** `-1` is returned
- **AND** an error message is logged

#### Scenario: handle == 0 guard

- **WHEN** a caller invokes `destroy_va_space(0)`
- **THEN** `-1` is returned without invoking ioctl
- **AND** no error is logged (sentinel path is a programming error, not runtime error)

### Requirement: `IGpuDriver::register_gpu(va_space_handle, gpu_id, flags) → int`

The `IGpuDriver` class MUST provide a method that calls `GPU_IOCTL_REGISTER_GPU` (`gpu_ioctl.h` line 184) to bind a GPU to a VA Space.

#### Scenario: Successful GPU registration

- **WHEN** a caller invokes `register_gpu(va_space_handle, gpu_id, flags)` with a valid `va_space_handle != 0` and a valid `gpu_id`
- **AND** the ioctl succeeds
- **THEN** `0` is returned

#### Scenario: va_space_handle == 0 guard (H-1 sentinel)

- **WHEN** a caller invokes `register_gpu(0, gpu_id, flags)`
- **THEN** `-1` is returned without invoking ioctl
- **AND** an error message is logged (guard explicitly rejects H-1 sentinel)

#### Scenario: ioctl failure

- **WHEN** the ioctl returns an error
- **THEN** `-1` is returned
- **AND** an error message is logged

### Requirement: `IGpuDriver::create_queue(va_space_handle, queue_type, priority, ring_buffer_size) → u64`

The `IGpuDriver` class MUST provide a method that calls `GPU_IOCTL_CREATE_QUEUE` (`gpu_ioctl.h` line 203) and returns the resulting queue handle as a `uint64_t`. The signature MUST include all 4 input fields (no abbreviation).

> **Note**: Returns `uint64_t queue_handle` (NOT `uint32_t`) — the upstream handler uses `next_queue_handle_++` monotonic counter (`gpgpu_device.cpp:412`). Truncating to u32 would break the R2 mapping contract (R6).

#### Scenario: Successful Queue creation

- **WHEN** a caller invokes `create_queue(va_space_handle, queue_type, priority, ring_buffer_size)` with `va_space_handle != 0`, valid `queue_type` (COMPUTE/COPY/GRAPHICS), `0 <= priority <= 100`, and a non-zero `ring_buffer_size`
- **AND** the ioctl succeeds
- **THEN** a `uint64_t queue_handle >= 1` is returned (monotonic from 1 per R2)

#### Scenario: va_space_handle == 0 guard

- **WHEN** a caller invokes `create_queue(0, ...)`
- **THEN** `0` is returned without invoking ioctl

#### Scenario: Invalid parameters

- **WHEN** a caller invokes `create_queue` with `priority > 100` OR `ring_buffer_size == 0`
- **THEN** `0` is returned
- **AND** an error message is logged identifying the invalid parameter

#### Scenario: ioctl failure

- **WHEN** the ioctl returns an error
- **THEN** `0` is returned
- **AND** an error message is logged

### Requirement: `IGpuDriver::destroy_queue(queue_handle) → int`

The `IGpuDriver` class MUST provide a method that calls `GPU_IOCTL_DESTROY_QUEUE` (`gpu_ioctl.h` line 217) for a given queue handle.

#### Scenario: Successful Queue destruction

- **WHEN** a caller invokes `destroy_queue(queue_handle)` with a valid `queue_handle != 0`
- **AND** the ioctl succeeds
- **THEN** `0` is returned

#### Scenario: queue_handle == 0 guard

- **WHEN** a caller invokes `destroy_queue(0)`
- **THEN** `-1` is returned without invoking ioctl

#### Scenario: ioctl failure

- **WHEN** the ioctl returns an error
- **THEN** `-1` is returned
- **AND** an error message is logged

### Requirement: R2 mapping enforcement — `stream_id = LOW32(queue_handle)`

The `submit_batch()` integration path MUST enforce the R2 mapping contract: when a caller creates a queue via `create_queue(...)` returning a `uint64_t queue_handle`, the caller MUST save the full `uint64_t` value and, when calling `submit_batch(stream_id, ...)`, set `stream_id = (uint32_t)queue_handle` (low 32 bits). The driver-side handler (`gpgpu_device.cpp:260-262`) zero-extends `args->stream_id` back to `uint64_t` and verifies it is in the VA Space's `attached_queues` list.

> **Note**: This is the contract that closes the loop between `create_queue` (u64) and `submit_batch` (u32). Failure to follow it (truncation, custom counters) causes silent `-EINVAL` from the upstream handler.

#### Scenario: Caller saves full u64 handle and submits with low 32 bits

- **WHEN** caller calls `create_queue(...)` and receives `queue_handle = 0x100000001` (high bits set)
- **AND** caller saves the full u64 value in own map
- **AND** caller later calls `submit_batch(stream_id=1, ...)` (the low 32 bits of 0x100000001)
- **THEN** upstream handler `static_cast<uint64_t>(1) == 1` matches the `attached_queues` entry
- **AND** the ioctl succeeds

#### Scenario: Caller truncates queue_handle to u32 prematurely

- **WHEN** caller converts `queue_handle = 0x100000001` to `uint32_t stream_id = 1` but loses the upper bits in its own tracking
- **AND** later attempts to destroy by re-deriving `queue_handle` from a different counter
- **THEN** `destroy_queue(...)` fails with `-EINVAL` (handle not found)

#### Scenario: Caller uses a custom counter instead of LOW32(handle)

- **WHEN** caller passes `stream_id = 42` to `submit_batch` (custom value, not derived from `queue_handle`)
- **THEN** upstream handler `static_cast<uint64_t>(42)` does not match the `attached_queues` entry from `create_queue`
- **AND** the ioctl returns `-EINVAL`

### Requirement: ioctl path does not require `MAP_QUEUE_RING`

The TaskRunner implementation of `submit_batch` (ioctl path) MUST NOT call `GPU_IOCTL_MAP_QUEUE_RING`. The upstream handler routes ioctl-path submissions through `GPFIFO_BASE + doorbell_ring` (per `gpgpu_device.cpp:284-300`).

> **Note**: `MAP_QUEUE_RING` is only required for the mmap fast path, which TaskRunner does not use in Phase 2. This is a deliberate scope choice (see `design.md §What's NOT In Scope`).

#### Scenario: TaskRunner uses ioctl path only

- **WHEN** TaskRunner calls `submit_batch` (ioctl path)
- **THEN** `MAP_QUEUE_RING` is NOT called by TaskRunner
- **AND** the upstream handler routes via `hal_doorbell_ring(hal_, args->stream_id)` successfully

### Requirement: Backward compatibility with H-1 callers

After the addition of 5 Phase 2 wrapper methods, all existing call sites (including H-1's `set_current_va_space` / `get_current_va_space` / `submit_batch`) MUST compile and behave identically.

> **Note**: D1 decision — `current_va_space_handle_` field in `GpuDriverClient` is RETAINED for H-1 compatibility, but `create_va_space()` does NOT auto-set it. Caller must explicitly call `set_current_va_space()` to trigger H-1 validation.

#### Scenario: H-1 sentinel path preserved

- **WHEN** existing callers use `submit_batch` without invoking any new Phase 2 method
- **AND** `set_current_va_space(0)` is the default (or never called)
- **THEN** `args.va_space_handle == 0` at ioctl time
- **AND** H-1 validation is skipped (sentinel path)
- **AND** behavior is identical to post-H-1 state

#### Scenario: H-1 + new method combination

- **WHEN** a caller invokes `create_va_space(flags)` returning handle `H`
- **AND** then invokes `set_current_va_space(H)`
- **AND** then invokes `submit_batch(stream_id, ...)`
- **THEN** `args.va_space_handle == H` (non-zero) at ioctl time
- **AND** H-1 validation runs against VA Space `H`

### Requirement: Tests via `IGpuDriver*` interface with `MockGpuDriver`

The 5 Phase 2 wrapper methods MUST be tested via `IGpuDriver*` interface injection with a `MockGpuDriver` test fixture. Tests MUST NOT trigger real `/dev/gpgpu0` ioctl calls.

> **Note**: Existing `tests/test_cuda_scheduler.cpp` tests `CudaScheduler + CudaStub`, NOT `GpuDriverClient`. A new test file `tests/test_gpu_phase2.cpp` is required (H-3 scope).

#### Scenario: 10 test cases cover success + guard paths

- **WHEN** `tests/test_gpu_phase2.cpp` is run
- **THEN** the test report shows 10 cases passing:
  - 5 success path: `create_va_space_returns_nonzero_handle`, `destroy_va_space_succeeds_with_valid_handle`, `register_gpu_succeeds_with_valid_va_space`, `create_queue_returns_u64_handle`, `destroy_queue_succeeds_with_valid_handle`
  - 4 guard/error path: `create_va_space_guard_when_closed`, `destroy_va_space_guard_when_handle_zero`, `register_gpu_guard_when_va_space_zero`, `create_queue_guard_when_va_space_zero`
  - 1 R2 mapping: `r2_mapping_stream_id_equals_low32_of_queue_handle`
- **AND** no real `/dev/gpgpu0` ioctl calls occur (mock records calls and returns canned values)
- **AND** existing `tests/test_cuda_scheduler.cpp` 8 cases remain passing (no regression)

#### Scenario: MockGpuDriver injection through DI

- **WHEN** a test fixture constructs `CudaScheduler` with `MockGpuDriver*` as the `IGpuDriver*` constructor argument
- **THEN** all 5 Phase 2 method calls route to `MockGpuDriver` (verified via `MockGpuDriver::last_call()`)
- **AND** switching the constructor argument to `GpuDriverClient*` (real) yields identical test outcomes (same assertions on return values)

## MODIFIED Requirements

_None — H-3 is purely additive. Existing capabilities (`gpu-pushbuffer-validation`, `gpu-pushbuffer-validation-deployment`, `gpu-driver-architecture`) are not modified._

## REMOVED Requirements

_None._

## Cross-references

- **H-2.5** (prerequisite): `gpu-driver-architecture` capability — `IGpuDriver` abstract interface in `include/igpu_driver.hpp` (5 Phase 2 methods declared at line 267-302)
- **H-1**: `gpu-pushbuffer-validation` capability — provides the validation logic that consumes the VA Space handles this capability creates
- **H-1 closeout**: `gpu-pushbuffer-validation-deployment` capability — deployment-layer artifact tracking
- **Upstream ADR**: UsrLinuxEmu ADR-024 Phase 2 (Accepted v1)
- **R2 mapping upstream**: `UsrLinuxEmu/plugins/gpu_driver/drv/gpgpu_device.cpp:260-262` (stream_id zero-extension lookup) + `gpgpu_device.cpp:412` (next_queue_handle_++ monotonic)
- **SSOT**: `docs/02_architecture/post-refactor-architecture.md` §1.3 (v0.1.5 待加)
- **H-7 ADR** (deferred): 3 owner-flagged upstream issues (stream_id u32 vs queue_handle u64 type mismatch / ioctl path bypasses GpuQueueEmu / attached_queues weak validation) — TaskRunner 侧不解决
- **DEPRECATED H-2** (history): `plans/2026-06-19-h2-phase2-openspec-skeleton/` — split source of this change
