---
SCOPE: TEST-FIXTURE
STATUS: ACTIVE
---

# test-fixture Scope

This directory contains all **test-fixture scope** content for TaskRunner — the currently-shippable state.

## In Scope

- `IGpuDriver` abstraction (28 methods, see `../shared/adr/tadr-301-igpu-driver-contract.md`)
- `GpuDriverClient` (real `/dev/gpgpu0` ioctl)
- `CudaStub` (in-memory mock with `next_handle_` monotonic + existence tracking)
- `MockGpuDriver` (headless test fixture)
- `CudaScheduler` + `cmd_cuda` CLI (6 subcommands)
- Tests: `test_cuda_scheduler` (8 cases), `test_gpu_architecture` (11 cases), `test_gpu_phase2` (12 cases)

## Subdirectories

- `architecture/` — Test-fixture architecture documentation
- `roadmap/` — Test-fixture roadmap (phase-1, phase-1.5, phase-2, phase-3)
- `archive/` — Historical DDS v1.0/v1.2 design docs (preserved, DEPRECATED)
- `phase1-week1-plan.md` — Phase 1 week-1 plan (historical, ✅ completed)
- `adr/` — Test-fixture scope TADRs (tadr-101~106 + redirect files for old tadr-004~008)

## Cross-Scope References

- For UMD evolution vision: see `../umd-evolution/README.md` `[UMD-EVOLUTION SCOPE]`
- For shared infrastructure: see `../shared/README.md`
