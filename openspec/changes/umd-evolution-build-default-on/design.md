---
SCOPE: umd-evolution
STATUS: PROPOSED
---

## Context

`CMakeLists.txt` (top-level) currently routes the build based on `TASKRUNNER_BUILD_MODE`:

```cmake
# canonical CMakeLists.txt:65-77 (simplified for clarity)
include(${CMAKE_SOURCE_DIR}/cmake/Shared.cmake)  # always

if(TASKRUNNER_BUILD_MODE STREQUAL "test-fixture")
    include(${CMAKE_SOURCE_DIR}/cmake/TestFixture.cmake)
elseif(TASKRUNNER_BUILD_MODE STREQUAL "umd-evolution")
    include(${CMAKE_SOURCE_DIR}/cmake/UMDEvolution.cmake)  # already includes TestFixture internally
else()
    message(FATAL_ERROR "Unknown TASKRUNNER_BUILD_MODE: ${TASKRUNNER_BUILD_MODE}")
endif()
```

This change releases the gate so `UMDEvolution.cmake` is invoked by default,
while preserving `TASKRUNNER_BUILD_MODE=test-fixture` as an opt-out.

## Important: actual CMake idiom is `include()`, not `add_subdirectory()`

The current build system is split into three `.cmake` fragment files
(`Shared.cmake`, `TestFixture.cmake`, `UMDEvolution.cmake`) that are `include()`d
by the top-level `CMakeLists.txt`. **It does not use `add_subdirectory()` for
the per-scope sources.** Any reviewer or implementer who translates the
high-level intent into `add_subdirectory(src/test_fixture)` + `add_subdirectory(src/umd)`
will break the build:

- `cmake/TestFixture.cmake:24` already adds `src/umd/cuda_runtime_api.cpp` to
  the `taskrunner` CLI target. A naive parallel `add_subdirectory(src/umd)` would
  create a second compilation target for the same source, leading to duplicate
  symbol errors at link time.
- `cmake/UMDEvolution.cmake:9` already does
  `include(${CMAKE_SOURCE_DIR}/cmake/TestFixture.cmake)`. So including
  `UMDEvolution.cmake` automatically includes test-fixture.
- `cmake/TestFixture.cmake:29` calls `enable_testing()`. Double-invoking it
  via both branches would generate a CMake warning (or error in strict mode).

The correct idiom is shown in Decision 1 below.

## Goals / Non-Goals

**Goals:**
- UMD code compiles by default (no env var required)
- Existing `$BASELINE_TEST_COUNT` tests still pass (count obtained via `ctest -N` in tasks.md step 1.1; project docs claim 318)
- test-fixture-only build path still works (backward compat)
- umd-evolution build path still works (kept for vision experiments)

**Non-Goals:**
- Removing the build-mode mechanism entirely
- Adding new cu* APIs
- Modifying CI scripts in scope (CI updates are tasks.md steps 5.4/5.6 — required for cross-repo consistency, not part of "CI scripts" in the change itself)

## Decisions

### Decision 1: Default-on, with explicit opt-out

```cmake
# new top-level routing in CMakeLists.txt
if(TASKRUNNER_BUILD_MODE STREQUAL "test-fixture")
    # opt-out: test-fixture-only (no libcuda_shim, no tests/umd)
    include(${CMAKE_SOURCE_DIR}/cmake/TestFixture.cmake)
else()
    # default: full UMD + test-fixture
    include(${CMAKE_SOURCE_DIR}/cmake/UMDEvolution.cmake)
    # UMDEvolution.cmake internally includes TestFixture.cmake (do NOT re-include here)
endif()
```

**Rationale**:
- Mirrors how most open-source C++ projects handle experimental features. The
  default is the "real" code; experimental modes are opt-in.
- Reuses the existing `UMDEvolution.cmake` chain rather than duplicating logic
  — `UMDEvolution.cmake` already pulls in `TestFixture.cmake`, so we don't
  need to include both manually.
- Preserves the gating variable name `TASKRUNNER_BUILD_MODE` for backward
  compatibility; existing CI scripts and docs that pass
  `-DTASKRUNNER_BUILD_MODE=umd-evolution` continue to work unchanged.

### Decision 2: Keep `TASKRUNNER_BUILD_MODE=umd-evolution` as alias for default

For backward compat with existing scripts and docs, `TASKRUNNER_BUILD_MODE=umd-evolution` continues to work (now treated as default).

**Rationale**: No need to break existing CI configs / dev workflows. The env var is now effectively a no-op for UMD, but still documented as the "experimental vision" path.

### Decision 3: Test-fixture mode excludes UMD shim + tests (but keeps `cuda_runtime_api.cpp`)

When `TASKRUNNER_BUILD_MODE=test-fixture`, the following UMD code is **not** compiled:
- `src/umd/libcuda_shim/*` (the LD_PRELOAD shim, 15 .cpp files)
- `tests/umd/*` (7 test executables for the shim)

However, `src/umd/cuda_runtime_api.cpp` **remains compiled** in test-fixture mode
because `cmake/TestFixture.cmake:24` includes it in the `taskrunner` CLI target.
This is intentional: the CLI needs the CUDA Runtime API surface even when the
shim is not built.

**Rationale**: Preserves the test-fixture scope's identity for users who only
want the architecture/correctness tests and the CLI — not the LD_PRELOAD shim
and its associated test suite.

## Risks / Trade-offs

| # | Risk | Mitigation |
|---|------|------------|
| 1 | Default build size grows (UMD adds ~30 cu* symbols + shim) | Acceptable trade-off; the symbols are tiny and the UMD code already builds in umd-evolution mode |
| 2 | UMD shim becomes part of `libtaskrunner.a` (or equivalent) | Verified: `cuda_taskrunner` and `taskrunner_umd_stub` are independent SHARED libraries; they are NOT merged into `taskrunner_test_fixture` (see `UMDEvolution.cmake:11-58`). No library-target refactor needed. |
| 3 | Some test-fixture tests depend on UMD symbols being absent | Verified by inspection: `tests/test_fixture/` does not link against `cuda_taskrunner` or `taskrunner_umd_stub`. Adding them to default build is additive, not subtractive. |
| 4 | CI matrix breaks (TaskRunner `.github/workflows/shim.yml`) | CI already passes `-DTASKRUNNER_BUILD_MODE=umd-evolution` (alias for new default) — will continue to pass. **NEW**: Add a separate matrix job without the flag to validate the new default mode (tasks.md step 5.4). |
| 5 | Cross-repo CI impact (UsrLinuxEmu `cmake-multi-platform.yml`, `perf-nightly.yml`) | These workflows do NOT pass `TASKRUNNER_BUILD_MODE`, so they will start building UMD by default. Tasks.md step 5.6 evaluates whether to pin `test-fixture` mode in those workflows. |
| 6 | `include/umd/cuda_runtime_api.hpp` is now in the default include path | Verified: header has `// SCOPE: UMD-EVOLUTION` annotation (line 1) and no `__cplusplus` guards or `#ifdef TASKRUNNER_UMD_EVOLUTION` blocks. Risk is informational; downstream consumers that include the header implicitly accept the experimental dependency. |
| 7 | Default-mode CI coverage gap (existing `.github/workflows/shim.yml` always sets the flag, so the new default is never CI-tested) | Mitigated by step 5.4 (add no-flag matrix job). Without this step, CI would never validate the production default behavior. |
| 8 | Supersedes an ACCEPTED ADR (tadr-108) — governance inconsistency | Mitigated by tasks.md step 2.6 (mark tadr-108 SUPERSEDED with pointer) and cross-repo mirror update (UsrLinuxEmu `docs/00_adr/README.md`). Without this step, the governance log is inconsistent. |

## Verification

```bash
# Mode 1: default (no env var) — NEW behavior
cmake -B build_default && cmake --build build_default -j4
ctest --test-dir build_default  # expect $BASELINE_TEST_COUNT

# Mode 2: test-fixture opt-out
TASKRUNNER_BUILD_MODE=test-fixture cmake -B build_tf && cmake --build build_tf -j4
ctest --test-dir build_tf  # expect test-fixture tests only (UMD tests absent)

# Mode 3: umd-evolution (backward compat alias)
TASKRUNNER_BUILD_MODE=umd-evolution cmake -B build_umd && cmake --build build_umd -j4
ctest --test-dir build_umd  # expect $BASELINE_TEST_COUNT (no regression)

# Duplicate-symbol check (tasks.md step 3.4)
nm build_default/libtaskrunner_umd_stub.so 2>/dev/null | grep -c cuda   # baseline
nm build_default/taskrunner 2>/dev/null | grep -c cuda                  # should match
nm -D --defined-only build_default/libcuda_taskrunner.so | grep -c cu   # ≥30
```

## Open Questions

(none — exact change scope is mechanical CMake)
