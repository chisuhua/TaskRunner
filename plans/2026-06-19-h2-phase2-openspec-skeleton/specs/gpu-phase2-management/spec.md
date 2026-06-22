# Capability: gpu-phase2-management

## ADDED Requirements

### Requirement: GpuDriverClient exposes Phase 2 VA Space creation method

The `GpuDriverClient` class MUST provide a method that calls `GPU_IOCTL_CREATE_VA_SPACE` and returns the resulting VA Space handle.

> **Note**: Method name is TBD pending design decision D3 (CamelCase vs snake_case).
> **Note**: Whether the returned handle is auto-stored in `current_va_space_handle_` (H-1 field) is TBD pending design decision D1.

#### Scenario: Successful VA Space creation

- **WHEN** a caller invokes the create method on an open `GpuDriverClient`
- **AND** the ioctl succeeds
- **THEN** a non-zero `gpu_va_space_handle_t` value is returned

#### Scenario: Failed VA Space creation

- **WHEN** a caller invokes the create method
- **AND** the ioctl returns an error (e.g., no device, insufficient privileges)
- **THEN** the method returns `0`
- **AND** an error message is logged to `std::cerr` including `errno`

#### Scenario: Method on closed client

- **WHEN** a caller invokes the create method on a `GpuDriverClient` where `is_open()` is `false`
- **THEN** the method returns `0` without invoking ioctl

### Requirement: GpuDriverClient exposes Phase 2 VA Space destruction method

The `GpuDriverClient` class MUST provide a method that calls `GPU_IOCTL_DESTROY_VA_SPACE` for a given VA Space handle.

#### Scenario: Successful VA Space destruction

- **WHEN** a caller invokes the destroy method with a valid handle
- **THEN** the ioctl is invoked
- **AND** `0` is returned on success

#### Scenario: Failed VA Space destruction

- **WHEN** the ioctl returns an error (e.g., invalid handle, VA Space still has attached queues)
- **THEN** `-1` is returned
- **AND** an error message is logged

### Requirement: GpuDriverClient exposes Phase 2 GPU registration method

The `GpuDriverClient` class MUST provide a method that calls `GPU_IOCTL_REGISTER_GPU` to bind a GPU to a VA Space.

#### Scenario: Successful GPU registration

- **WHEN** a caller invokes the register method with a valid `va_space_handle` and `gpu_id`
- **THEN** the ioctl is invoked
- **AND** `0` is returned on success

#### Scenario: Invalid VA Space handle

- **WHEN** a caller invokes the register method with `va_space_handle == 0`
- **THEN** `-1` is returned without invoking ioctl

### Requirement: GpuDriverClient exposes Phase 2 Queue creation method

The `GpuDriverClient` class MUST provide a method that calls `GPU_IOCTL_CREATE_QUEUE` and returns the resulting queue id.

#### Scenario: Successful Queue creation

- **WHEN** a caller invokes the create method with a valid `va_space_handle`
- **AND** the ioctl succeeds
- **THEN** a non-zero queue id is returned

#### Scenario: VA Space validation

- **WHEN** a caller invokes the create method with `va_space_handle == 0`
- **THEN** the method returns `0` without invoking ioctl (matches H-1 sentinel pattern)

### Requirement: GpuDriverClient exposes Phase 2 Queue destruction method

The `GpuDriverClient` class MUST provide a method that calls `GPU_IOCTL_DESTROY_QUEUE` for a given queue id.

#### Scenario: Successful Queue destruction

- **WHEN** a caller invokes the destroy method with a valid `queue_id`
- **THEN** the ioctl is invoked
- **AND** `0` is returned on success

### Requirement: Backward compatibility with H-1 callers

After the addition of Phase 2 wrapper methods, all existing call sites (including H-1's `setCurrentVASpace()` / `getCurrentVASpace()` / `submit_batch()`) MUST compile and behave identically.

#### Scenario: H-1 callers unaffected

- **WHEN** existing callers use `setCurrentVASpace()` / `submit_batch()` without invoking any new Phase 2 method
- **THEN** behavior is identical to post-H-1 state
- **AND** `args.va_space_handle` is `0` at ioctl time (sentinel pattern)
- **AND** H-1 validation is skipped

#### Scenario: H-1 + new method combination

- **WHEN** a caller invokes `createVASpace()` (or equivalent D3-named method)
- **AND** then invokes `setCurrentVASpace(returned_handle)`
- **AND** then invokes `submit_batch()`
- **THEN** `args.va_space_handle` is the created handle (non-zero)
- **AND** H-1 validation runs against that VA Space

### Requirement: Phase 2 wrapper methods have doctest coverage

Each of the 5 Phase 2 wrapper methods MUST have at least one test case in `tests/test_cuda_scheduler.cpp` that exercises the success path in stub mode.

#### Scenario: All 5 wrappers tested

- **WHEN** `test_cuda_scheduler` is run
- **THEN** the test report shows the 5 new test cases passing
- **AND** the total test count increases from 8 to 13
- **AND** no existing test cases regress

### Requirement: Sync point S5 closed in TaskRunner plans

The `plans/sync-plan.md` MUST be updated to reflect that the Phase 2 wrapper delivery (S5 sync point) is complete.

#### Scenario: S5 marked complete

- **WHEN** the H-2 change is applied
- **THEN** `sync-plan.md` lines 247-249 (Phase 2 ioctls) show "✅ 已完成"
- **AND** line 265 (S5 row) shows completion date and PR reference

### Requirement: AGENTS.md reflects Phase 2 status

The `AGENTS.md` "Phase 1.5 进度" section MUST be updated to include Phase 2 management API as a completed milestone.

#### Scenario: Phase 2 milestone visible

- **WHEN** the H-2 change is applied
- **THEN** `AGENTS.md` contains a line indicating Phase 2 management API is delivered
- **AND** references both TaskRunner PR (featuring 5 wrappers) and UsrLinuxEmu openspec change `h2-phase2-implementation`

## MODIFIED Requirements

_None — H-2 is purely additive. Existing capabilities (`gpu-pushbuffer-validation`, `gpu-pushbuffer-validation-deployment`) are not modified._

## REMOVED Requirements

_None._

## Cross-references

- **H-1 capability**: `gpu-pushbuffer-validation` (provides the validation logic that consumes the VA Space handles this capability creates)
- **H-1 closeout capability**: `gpu-pushbuffer-validation-deployment` (deployment-layer artifact tracking, not behavior)
- **Upstream ADR**: UsrLinuxEmu ADR-024 Phase 2 (Accepted v1)
- **SSOT**: `docs/02_architecture/post-refactor-architecture.md` §1.3 (v0.1.5 待加)