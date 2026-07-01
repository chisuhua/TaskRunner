---
SCOPE: UMD-EVOLUTION
STATUS: DEFERRED
PHASE: 3
DEFERRED_DATE: 2026-07-01
TRIGGER_GATING: Yes
ESTIMATED_EFFORT: 3-5 weeks
---

# Phase 3 DEFERRED — API extension

**Goal**: Expand cu\* API coverage from 79 to ~150+ APIs, add Stream async support,
Memory pool, and richer Event semantics. NOT a continuation of Phase 2 in the
current session; deferred until external triggers materialize.

## Current Coverage (Phase 2 baseline)

| Category | Phase 2 coverage |
|----------|-------------------|
| Initialization | cuInit, cuDriverGetVersion, cuDriverGet |
| Device | cuDeviceGetCount, cuDeviceGet, cuDeviceGetName, cuDeviceGetAttribute, cuDeviceTotalMem, cuDeviceComputeCapability, cuDevicePrimaryCtx\* (5) |
| Context | cuCtxCreate/Destroy/SetCurrent/GetCurrent/Push/Pop/Synchronize, cuCtxGetDevice/Flags/ApiVersion, cuCtxGet/SetCacheConfig, cuCtxGet/SetSharedMemConfig, cuCtxGet/SetLimit |
| Module | cuModuleLoad, cuModuleUnload, cuModuleGetFunction, cuModuleGetGlobal, cuModuleLoadData/Ex, cuModuleLoadFatBinary |
| Memory | cuMemAlloc, cuMemFree, cuMemcpy (generic), cuMemcpyHtoD/DtoH/DtoD, cuMemcpyAsync (stub), cuMemsetD32/D8, cuMemAllocHost/FreeHost, cuMemAllocManaged/Pitch, cuMemGetInfo, cuMemGetAddressRange |
| Launch | cuLaunchKernel, cuLaunchKernelEx, cuLaunchHostFunc (NI), cuLaunchCooperativeKernel |
| Stream | cuStreamCreate/Destroy/Synchronize/Query, cuStreamWaitEvent, cuStreamAddCallback, cuStreamWaitValue32/WriteValue32, cuStreamGetPriority/Flags/CaptureInfo, cuStreamBegin/EndCapture (NI) |
| Event | cuEventCreate/Destroy/Record/Synchronize/Query/ElapsedTime |
| Error | cuGetErrorName, cuGetErrorString |

Total: **79 symbols** (41 critical implemented + 38 functional stubs).

## Phase 3 Priority Matrix

| Pri | Category | Sub-effort | Phase |
|-----|----------|------------|-------|
| **P0** | Stream async ops (cuStreamBeginCapture/EndCapture, graphs) | 1-2 w | 3.1 |
| **P0** | Memory pool (cuMemPool\*, cuMemAllocFromPoolAsync) | 1-2 w | 3.2 |
| **P1** | Event timing precision (proper CUDA clock API integration) | 1 w | 3.2 |
| **P1** | Texture/Surface (cuTexRefCreate/Destroy, cuArray\*) | 2 w | 3.3 |
| **P2** | YAML kernel registry (replace manual register_kernel with config file) | 1 w | 3.4 |
| **P2** | cuDeviceGetAttribute expansion (cover all attributes) | 0.5 w | 3.4 |
| **P3** | Multi-device support (cuDeviceGetCount > 1) | 2-3 w | 3.5 |
| **P3** | ELF/CUBIN parsing (D-3 lite, for real vectorAdd E2E) | 4-6 w | backlog |

## Open Decisions for Phase 3 Kickoff

| Q# | Question | Default | Notes |
|----|----------|---------|-------|
| Q1 | Cubin parsing strategy | YAML config | ELF reserved for D-3 |
| Q2 | Scope | All P0+P1 (~6 w) | P2 optional |
| Q3 | Vulkan arch extension | Keep (no impl) | matches Phase 0 decision |
| Q4 | Q4 POA | ✅ Resolved POA-1+POA-2 | no change needed |
| Q5 | Implementation team | Session-driven | works for Phase 2 |

## Trigger Conditions for Phase 3 Kickoff

Phase 3 should start when one of:

1. **UsrLinuxEmu Stage 1.4 starts** requiring cuStream/cuMemPool APIs
2. **External demand**: Bug report or user request for specific cu\* APIs
3. **CI gap**: tests requiring APIs that aren't currently stubbed
4. **Time pressure**: 4+ weeks idle (recurring review needed anyway)

Absence of these is why Phase 3 is **deferred, not cancelled**.

## What to Do When Phase 3 Starts

1. Create `docs/superpowers/plans/2026-XX-XX-umd-phase3-api-extension.md`
2. Update `docs/umd-evolution/architecture/runtime-layering.md` Phase 3 matrix section
3. TADRs: Add new tadr-21X series for Phase 3 decisions
4. Reuse the Phase 2 stub generator: `tools/generate_cu_stubs.py` already parameterized
5. Commit each phase sub-task with `feat(shim): ... (3.X)` prefix

## Phase 3 Estimated Effort

Sub-plan 3.1 (Stream async + Memory pool): 3-4 weeks
Sub-plan 3.2 (Event precision + Texture/Surface): 2-3 weeks
Sub-plan 3.3 (YAML registry + Multi-device + ELF parse): 6-9 weeks

**Total Phase 3**: 11-16 weeks (3-4 months)

If only 3.1 + 3.2 (no 3.3): 5-7 weeks (1.5 months)

## Plans That Exist Already

- Phase 2 plan v2 with 12 tasks (`88603ed`) — useful template for Phase 3 plan

## Forbidden in Phase 3 (maintain scope)

- ❌ Vulkan Runtime API implementation (Q3 decision: no)
- ❌ Real kernel execution via ELF parsing (deferred to D-3)
- ❌ Replacing CudaStub with real device backend (CudaStub stays as test target)

## Related Backlog Items (not Phase 3)

- **Phase 1.5 Stretch**: Fix `dynamic_cast<CudaStub*>` 5 sites in CudaScheduler
  to enable GpuDriverClient backend test path. Files:
  `src/test_fixture/cuda_scheduler.cpp`. Effort: 0.5-1 d.

- **D-1 / D-3** (deferred per `gap-analysis.md` indefinitely):
  D-1 requires UsrLinuxEmu doorbell mmap (ADR-024)
  D-3 requires UsrLinuxEmu BasicGpuSimulator kernel execution

For Phase 3 + Phase 1.5, the recommended next session workflow:
1. User says "let's start Phase 3.1"
2. New session reads this roadmap
3. Creates detailed plan in `docs/superpowers/plans/`
4. Dispatches C.3.1.x implementer subagents
5. Pushes commits to `main` with `feat(shim): ... (Phase 3.1)` prefix
