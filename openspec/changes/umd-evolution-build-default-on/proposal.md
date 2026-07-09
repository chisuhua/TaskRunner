---
SCOPE: umd-evolution
STATUS: PROPOSED
---

## Why

UMD-EVOLUTION scope (per `docs/umd-evolution/README.md`) currently has STATUS: PROPOSED with the hard rule:

> **`STATUS: ACCEPTED` is FORBIDDEN** for unimplemented features.

To promote to ACCEPTED, the umd-evolution code must be **shippable by default** — not gated behind a build-mode toggle. The current CMakeLists.txt / `cmake/UMDEvolution.cmake` includes `src/umd/libcuda_shim/` and `tests/umd/` only when `TASKRUNNER_BUILD_MODE=umd-evolution` is set. This is a **build-mode gate** that signals "experimental / vision-only" to anyone reading the build system.

This change releases the gate so UMD code is the default. It is **entry condition 1/5** for UMD-EVOLUTION → ACCEPTED promotion.

## What Changes

In `CMakeLists.txt` and `cmake/UMDEvolution.cmake`:
- **Default behavior**: `add_subdirectory(src/umd)` and `add_subdirectory(tests/umd)` always run
- **`TASKRUNNER_BUILD_MODE=test-fixture`**: produces test-fixture-only build (excludes UMD)
- **`TASKRUNNER_BUILD_MODE=umd-evolution`**: same as default (kept for backward compat / vision experimentation)

Net effect: existing users who don't set the env var now get UMD code by default; users who explicitly want test-fixture-only opt out via the env var.

## Capabilities

### New Capabilities

(none — this is a build-system promotion, not a feature addition)

### Modified Capabilities

(none)

## Impact

- **Files affected**: 
  - `CMakeLists.txt` (top-level)
  - `cmake/UMDEvolution.cmake`
  - Possibly `cmake/TestFixture.cmake` (mirror change for test-fixture exclusion)
- **No production code changes**
- **No API changes**
- **No new tests** (existing 318 tests must continue to pass)
- **No cross-repo changes** (TaskRunner-only)

## Acceptance Criteria

- `cmake -B build` (no env var) compiles `src/umd/libcuda_shim/*.cpp`
- `ctest --test-dir build` shows all 318 tests pass
- `TASKRUNNER_BUILD_MODE=test-fixture cmake -B build` produces a build that excludes UMD sources (backward compat)
- `TASKRUNNER_BUILD_MODE=umd-evolution cmake -B build` still works (no regression)
- `tools/docs-audit.sh` passes

## Risk

- **Build system blast radius**: changing add_subdirectory order/conditions can have subtle effects on target dependencies. Mitigated by running both default-mode and test-fixture-mode builds.
- **CI matrix**: if CI uses test-fixture mode, this change makes UMD the default. Need to verify CI doesn't break.
- **Cross-platform**: Windows / macOS builds haven't been tested; the change is Linux-only in scope initially.
