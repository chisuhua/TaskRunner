---
SCOPE: TEST-FIXTURE
STATUS: ACCEPTED
DECISION_DATE: 2026-06-24
---

# ADR: test-fixture Scope Clarification (H-5)

## Context

TaskRunner submodule previously mixed test-fixture code (H-1/H-2.5/H-3 shippable) with UMD vision content (tadr-001~003 unimplemented). This caused confusion about what is currently working vs what is aspirational.

## Decision

Formally establish the **test-fixture** scope as the default-main state of TaskRunner.

**In scope:**
- `IGpuDriver` 28-method abstraction
- `GpuDriverClient` (real `/dev/gpgpu0` ioctl)
- `CudaStub` (in-memory mock with `next_handle_` + existence tracking)
- `MockGpuDriver` (headless test fixture)
- `CudaScheduler` + `cmd_cuda` CLI (6 subcommands)
- Tests: `test_cuda_scheduler` (8 cases), `test_gpu_architecture` (11 cases), `test_gpu_phase2` (12 cases)

**Out of scope (belongs to umd-evolution):**
- Real CUDA Runtime API surface
- CUmodule/CUfunction loading
- Doorbell mmap bypass
- Ring buffer self-management
- Stream object model
- Context (CUcontext) model
- Unified Memory page table

## Consequences

Positive:
- Clear scope separation eliminates confusion
- New contributors can quickly identify "what's working" vs "what's vision"
- H-3.5 follow-up work has unambiguous scope

Negative:
- Documentation overhead (3 separate scope directories)
- Dual review requirement for shared-scope changes

## References

- tadr-101 through tadr-105 (existing test-fixture ADRs)
- H-5 proposal: `openspec/changes/h5-taskrunner-scope-clarification/proposal.md`
