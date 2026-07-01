---
SCOPE: UMD-EVOLUTION
STATUS: ACTIVE
LAST_UPDATED: 2026-07-01
HEAD_COMMIT: 83ef131
TESTS: 76/76
AUDIT: 54/54
---

# Current Status (2026-07-01)

## TL;DR

UMD-EVOLUTION redesign complete through Phase 2. **Phase 3** (API extension:
Stream, Event, Memory pool) is the only remaining scope and is currently DEFERRED
pending external triggers. All implementation work has been pushed to
`origin/main` on TaskRunner and synced to UsrLinuxEmu.

For continuation, see phase-specific roadmap files:
- [`phase-2-complete.md`](phase-2-complete.md) — most recently completed
- [`phase-3-deferred.md`](phase-3-deferred.md) — what's next

## Branch & Commit State

```
TaskRunner:  main @ 83ef131 (pushed to origin/main)
UsrLinuxEmu: main @ 9d23125 (submodule pointer synced to 83ef131)
```

## Test Status

| Suite | Cases | Result |
|-------|-------|--------|
| test_cuda_scheduler | 8 | ✅ pass |
| test_gpu_architecture | 11 | ✅ pass |
| test_gpu_phase2 | 12 | ✅ pass |
| test_cuda_runtime_api (P1) | 8 | ✅ pass |
| test_cuda_shim (P2) | 37 | ✅ pass |
| **Total** | **76** | **✅ 100% pass** |
| docs-audit.sh | 54 checks | ✅ pass |

## How to Verify Everything Still Works (5 minutes)

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner

# Configure & build (UMD-EVOLUTION mode)
cd build && cmake .. -DTASKRUNNER_BUILD_MODE=umd-evolution
make -j4
cd ..

# Run all tests
for t in test_cuda_scheduler test_gpu_architecture test_gpu_phase2 \
          test_cuda_runtime_api test_cuda_shim; do
  ./build/$t 2>&1 | tail -1
done

# Verify shim binary
nm -D --defined-only build/libcuda_taskrunner.so | grep -c " cu[A-Z]"
# Should print: 79

# Doc audit
./tools/docs-audit.sh 2>&1 | tail -5
```

Expected: 76/76 tests pass · 79 cu\* symbols · docs-audit 54/54 ✓

## Where to Start in a New Session

If continuing work in a new session, follow this order:

1. **Read [`phase-2-complete.md`](phase-2-complete.md)** — most recent phase
2. **Read [`phase-3-deferred.md`](phase-3-deferred.md)** — pending work + triggers
3. If user asks to start Phase 3, read:
   - Design spec: `docs/superpowers/specs/2026-06-30-umd-evolution-redesign.md` §Phase 3
4. If user asks about Phase 2 verification, just rerun the 5-minute check above

## Phase 0 Summary (Doc Fix)

3 TADRs (`tadr-201/202/203`) changed status from PROPOSED-but-actually-implemented
to **SUPERSEDED**. Created `architecture/` directory with 2 docs (README + runtime-layering).
Added conflict resolution table. Fixed 8 broken archive paths.

## Phase 1 Summary (Runtime PoC)

`CudaRuntimeApi` class with 3 methods:
- `cudaMalloc(void**, size)`
- `memcpy(void*, const void*, size, kind)`
- `launch_kernel(name, grid, block, args, sharedMem)`

Built on existing `CudaScheduler` (H-5) — **not bypassing the scheduler** (Oracle review correct correction).
8 doctest cases + 4 CLI commands. Builds in `TASKRUNNER_BUILD_MODE=umd-evolution`.

## Phase 2 Summary (LD_PRELOAD shim)

`libcuda_taskrunner.so` (802KB) exporting **79 cu\* symbols**.

- 41 critical APIs implemented (cuInit, cuDevice\*, cuCtx\*, cuModule\*, cuMem\*, cuLaunchKernel, cuDriverGetVersion, cuDevicePrimaryCtx\*, etc.)
- 38 functional stubs returning CUDA_ERROR_NOT_IMPLEMENTED
- Oracle Critical #1 fix: cuModuleUnload cleans up function handles
- Oracle Critical #4 fix: cuCtx uses stack-tracked contexts (not hardcoded 0x1)
- CudaStub backend (single-device) provides the runtime semantics

**Usage:** Compile a CUDA program normally → `LD_PRELOAD=./libcuda_taskrunner.so ./myapp`
(Phase 3 needed for full real-kernel execution via D-3 ELF parsing).

## What's NOT Implemented (Future Work)

### Phase 3 — API extension (deferred, ~3-5 weeks if started)

Trigger conditions for Phase 3 kickoff:

1. UsrLinuxEmu Stage 1.4 starts requiring expanded CUDA API surface
2. External demand for additional cu\* APIs (Stream async, Memory pool)
3. Test-fixture CI coverage needed for more APIs

See [`phase-3-deferred.md`](phase-3-deferred.md) for breakdown.

### Phase D-1 / D-3 (deferred indefinitely per `gap-analysis.md`)

- D-1: Doorbell mmap bypass (requires UsrLinuxEmu ADR-024 implementation)
- D-3: ELF/CUBIN parser + real kernel execution (requires UsrLinuxEmu kernel ABI)

These remain blocked on UsrLinuxEmu-side infrastructure work.

## Known Limitations

See `architecture/runtime-layering.md` §Handle Lifecycle for full list:

1. No real kernel execution (cuLaunchKernel via CudaStub)
2. ~38 stub cu\* functions (intentional, return NOT_IMPLEMENTED)
3. cuMemcpyDtoD returns NOT_SUPPORTED (Phase 1 limitation)
4. Async stream capture not supported (graphs)
5. Single-device only
6. Thread-local context state (no cross-thread propagation)

## Open Questions (from design doc)

The design doc `2026-06-30-umd-evolution-redesign.md` had 5 open questions:

| Q# | Status | Resolution |
|----|--------|-----------|
| Q1 — YAML vs ELF parsing | ⚠️ Reserved | Default YAML (recommended); ELF deferred to D-3 |
| Q2 — Phase 3 scope (P0+P1 or include P2) | ⚠️ Reserved | Decision deferred with Phase 3 kickoff |
| Q3 — Vulkan extension points | ✅ RESOLVED | Architectural reservation kept (no implementation) |
| Q4 — Explicit PoC requirement | ✅ RESOLVED | POA-1 (KFD Consumer) + POA-2 (CI Regression) |
| Q5 — Spec/implementation team | ✅ RESOLVED | Session-driven implementation (this session) |

## File Map (where everything is)

```
docs/umd-evolution/                            # UMD-EVOLUTION scope root
├── README.md                                  # Scope overview
├── vision.md / vision-source.md               # Vision docs
├── gap-analysis.md                            # ROI decision authority
├── architecture/                              # Architecture docs (Phase 0)
│   ├── README.md                              #   Component diagram
│   └── runtime-layering.md                    #   Phase 1+2+handle lifecycle
├── adr/                                       # 5 ADRs (TADRs)
├── research/                                  # 2 external research docs (NVIDIA, AMD)
├── roadmap/                                   # THIS directory (state tracking)  ★
│   ├── README.md                              #   index
│   ├── current-status.md                      #   master snapshot           ★ read first
│   ├── phase-0-complete.md                    #   Phase 0 summary
│   ├── phase-1-complete.md                    #   Phase 1 summary
│   ├── phase-2-complete.md                    #   Phase 2 summary ★ most recent
│   └── phase-3-deferred.md                    #   Phase 3 deferred work
└── (Phase 2 source code is in src/umd/libcuda_shim/)

docs/superpowers/
├── specs/2026-06-30-umd-evolution-redesign.md # Design spec (authoritative)
├── plans/2026-06-30-umd-evolution-redesign.md # Phase 1 plan (B.1-B.7)
└── plans/2026-07-01-umd-phase2-ld-preload.md  # Phase 2 plan v2 (C.1-C.9)

src/umd/
├── cuda_runtime_api.hpp / .cpp                # Phase 1: CudaRuntimeApi
├── libcuda_shim/                              # Phase 2: cu* shim
│   ├── cu_init.cpp / cu_module.cpp
│   ├── cu_mem.cpp / cu_launch.cpp
│   ├── cu_ctx.cpp / cu_device.cpp
│   ├── cu_query.cpp / cu_stream.cpp / cu_event.cpp
│   └── cu_stub_table.inc
├── include/cuda.h                             # Compat cuda.h for shim builds

tests/umd/
├── test_cuda_runtime_api.cpp                  # Phase 1: 8 tests
└── test_cuda_shim.cpp                         # Phase 2: 37 tests

build/
├── test_cuda_scheduler / test_gpu_* / ...     # Phase 1+2 test binaries
├── libcuda_taskrunner.so                       # Phase 2 shim (LD_PRELOAD)
└── libcuda_taskrunner.so → 79 cu* symbols

tools/
├── docs-audit.sh                              # 54-check docs audit
└── generate_cu_stubs.py                       # Phase 2 stub generator
```

## How to Recover from a Bad State

If something is broken:

1. `git status` and `git log` — see state
2. `git log --oneline 88603ed..HEAD` — list Phase 2 commits
3. `./tools/docs-audit.sh` — verify docs structure
4. Forgot oracle review feedback? Check `plan v2 (88603ed)` — it includes all Oracle fixes applied
5. Re-push if missing: `git push origin main` (TaskRunner) + bump UsrLinuxEmu submodule
