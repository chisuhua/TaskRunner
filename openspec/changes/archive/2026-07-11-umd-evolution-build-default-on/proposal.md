---
SCOPE: umd-evolution
STATUS: PROPOSED
SUPERSEDES: [tadr-108-build-mode-selection]
RELATED:
  - umd-evolution-acceptance-promotion-adr (entry 5/5)
  - g-gpu-client-default-stub-init (entry 2/5)
  - l1-l2-bridge-e2e-test-skeleton (entry 3/5)
---

> **SUPERSEDES tadr-108**: This change directly reverses the canonical decision in
> `docs/shared/adr/tadr-108-build-mode-selection.md` (STATUS: ACCEPTED, 2026-06-25)
> which mandated "default = test-fixture". Per ADR-035 §supersede protocol, this
> change **must** update tadr-108 to STATUS: SUPERSEDED with a SUPERSEDED_BY pointer
> (see tasks.md §2.6). Without that step, the change leaves the governance log
> inconsistent.

## Why

UMD-EVOLUTION scope (per `docs/umd-evolution/README.md`) currently has STATUS: PROPOSED with the hard rule:

> **`STATUS: ACCEPTED` is FORBIDDEN** for unimplemented features.

To promote to ACCEPTED, the umd-evolution code must be **shippable by default** — not gated behind a build-mode toggle. The current CMakeLists.txt / `cmake/UMDEvolution.cmake` includes `src/umd/libcuda_shim/` and `tests/umd/` only when `TASKRUNNER_BUILD_MODE=umd-evolution` is set. This is a **build-mode gate** that signals "experimental / vision-only" to anyone reading the build system.

This change releases the gate so UMD code is the default. It is **entry condition 1/5** for UMD-EVOLUTION → ACCEPTED promotion.

## Scope Clarification: "shippable" vs "production-ready"

> **IMPORTANT**: "Shippable by default" here means **the build system compiles UMD
> code without an opt-in flag**. It does NOT mean the UMD shim is production-ready.
> Per `docs/umd-evolution/roadmap/`, UMD remains feature-incomplete (Phase 3.3+
> complete, Phase 4 partial); functional gaps are tracked in tadr-205. The CMake
> comment "experimental, not for production" in `cmake/UMDEvolution.cmake:3`
> reflects the latter; this change relaxes only the build-mode coupling.

### Caveat: `src/umd/cuda_runtime_api.cpp` already in test-fixture mode

A subtle but important detail: even under the current `test-fixture` default,
`cmake/TestFixture.cmake:24` already compiles `src/umd/cuda_runtime_api.cpp`
as part of the `taskrunner` CLI target. So "test-fixture mode excludes UMD" is
**only true for `src/umd/libcuda_shim/*` and `tests/umd/`**. After this change,
test-fixture opt-out excludes the same two paths; the CLI's UMD dependency is
unchanged.

## What Changes

In `CMakeLists.txt` (top-level routing only — source files are unaffected):
- **Default behavior**: `include(cmake/UMDEvolution.cmake)` runs, which transitively pulls in `TestFixture.cmake` and all `src/umd/libcuda_shim/*` + `tests/umd/*` sources.
- **`TASKRUNNER_BUILD_MODE=test-fixture`**: only `include(cmake/TestFixture.cmake)` runs (opt-out). The CLI's `cuda_runtime_api.cpp` dependency is preserved.
- **`TASKRUNNER_BUILD_MODE=umd-evolution`**: same as default (kept for backward compat / vision experimentation — alias).

> **IMPORTANT**: The change uses `include()` of `.cmake` fragments, NOT
> `add_subdirectory()`. See design.md §"Important: actual CMake idiom is
> `include()`, not `add_subdirectory()`" for why naive `add_subdirectory()`
> translation would break the build.

Net effect: existing users who don't set the env var now get UMD code by default; users who explicitly want test-fixture-only opt out via the env var.

## Capabilities

### New Capabilities

(none — this is a build-system promotion, not a feature addition)

### Modified Capabilities

(none)

## Impact

- **Files affected**: 
  - `CMakeLists.txt` (top-level — invert the if/else routing block)
  - `cmake/UMDEvolution.cmake` (decouple from env var; simplify now-redundant comments)
  - `docs/shared/adr/tadr-108-build-mode-selection.md` (mark SUPERSEDED, link to this change)
  - `AGENTS.md` §Build Mode Selection (update default description)
  - `docs/umd-evolution/roadmap/current-status.md` (note default-on transition)
  - `docs/ONBOARDING.md` (4 references to "仅在 TASKRUNNER_BUILD_MODE=umd-evolution 下编译")
  - `docs/superpowers/plans/2026-06-24-h5-taskrunner-scope-clarification.md` (5+ references to "default = test-fixture")
  - `.github/workflows/shim.yml` (add default-mode CI matrix entry)
- **No production source code changes** (`src/`, `include/`)
- **No API changes**
- **No new tests** (existing 318 tests must continue to pass; authoritative count obtained via `ctest -N`)
- **Cross-repo impact (NON-ZERO — was previously misstated)**:
  - UsrLinuxEmu `external/TaskRunner` submodule pointer MUST be bumped per AGENTS.md §跨仓工作原则 step 2
  - UsrLinuxEmu `.github/workflows/cmake-multi-platform.yml` (uses TaskRunner default mode → may now build UMD, ~5K LOC, build-time impact)
  - UsrLinuxEmu `.github/workflows/perf-nightly.yml` (uses default → perf baseline shifts)
  - UsrLinuxEmu `docs/00_adr/README.md` "TaskRunner TADR mirror" section: mark tadr-108 as SUPERSEDED

## Acceptance Criteria

- `cmake -B build` (no env var) compiles `src/umd/libcuda_shim/*.cpp`
- `ctest --test-dir build` shows all tests pass (count obtained via `ctest --test-dir build -N | wc -l`, baseline recorded in tasks.md step 1.1)
- `TASKRUNNER_BUILD_MODE=test-fixture cmake -B build` produces a build that excludes `src/umd/libcuda_shim/*` and `tests/umd/` (backward compat; `cuda_runtime_api.cpp` still compiled as part of CLI)
- `TASKRUNNER_BUILD_MODE=umd-evolution cmake -B build` still works (no regression — alias for default)
- No duplicate symbol errors (verify `nm build/lib* | grep cuda` and ctest output free of "multiple definition")
- `tools/docs-audit.sh` passes in default mode
- `tadr-108-build-mode-selection.md` is updated to STATUS: SUPERSEDED with SUPERSEDED_BY pointer
- UsrLinuxEmu submodule pointer bumped and at least one UsrLinuxEmu CI job passes with new pointer
- `.github/workflows/shim.yml` includes a default-mode (no flag) matrix job

## Risk

- **Build system blast radius**: changing the top-level `include()` routing can have subtle effects on target dependencies (e.g., `cuda_runtime_api.cpp` is referenced by both `taskrunner` CLI and `taskrunner_umd_stub`). Mitigated by running all 3 build modes and verifying no "multiple definition" errors (tasks.md step 3.4).
- **CI matrix**: existing CI uses `-DTASKRUNNER_BUILD_MODE=umd-evolution` (alias for new default) so passes through; but the **new default behavior has zero CI coverage** until step 5.4 adds a no-flag matrix job.
- **Cross-repo blast radius**: changes default `cmake -B build` behavior for ALL downstream consumers (UsrLinuxEmu CI workflows, downstream packaging). Mitigated by steps 5.3/5.5/5.6/5.7.
- **Cross-platform**: Windows / macOS builds haven't been tested; the change is Linux-only in scope initially.
