---
SCOPE: umd-evolution
STATUS: PROPOSED
---

## Context

`CMakeLists.txt` (top-level) currently routes the build based on `TASKRUNNER_BUILD_MODE`:

```cmake
# pseudo
if(TASKRUNNER_BUILD_MODE STREQUAL "umd-evolution")
  add_subdirectory(src/umd)
  add_subdirectory(tests/umd)
else()
  # default: test-fixture only
  add_subdirectory(src/test_fixture)
  add_subdirectory(tests/test_fixture)
endif()
```

This change releases the gate so `src/umd/` and `tests/umd/` are included by default, while preserving `TASKRUNNER_BUILD_MODE=test-fixture` as an opt-out.

## Goals / Non-Goals

**Goals:**
- UMD code compiles by default (no env var required)
- Existing 318 tests still pass
- test-fixture-only build path still works (backward compat)
- umd-evolution build path still works (kept for vision experiments)

**Non-Goals:**
- Removing the build-mode mechanism entirely
- Adding new cu* APIs
- Modifying CI scripts (out of scope; CI will follow once this change lands)

## Decisions

### Decision 1: Default-on, with explicit opt-out

```cmake
# new structure
if(TASKRUNNER_BUILD_MODE STREQUAL "test-fixture")
  # test-fixture-only mode (opt-out)
  add_subdirectory(src/test_fixture)
  add_subdirectory(tests/test_fixture)
else()
  # default: full UMD + test-fixture
  add_subdirectory(src/test_fixture)
  add_subdirectory(src/umd)
  add_subdirectory(tests/test_fixture)
  add_subdirectory(tests/umd)
endif()
```

**Rationale**: Mirrors how most open-source C++ projects handle experimental features. The default is the "real" code; experimental modes are opt-in.

### Decision 2: Keep `TASKRUNNER_BUILD_MODE=umd-evolution` as alias for default

For backward compat with existing scripts and docs, `TASKRUNNER_BUILD_MODE=umd-evolution` continues to work (now treated as default).

**Rationale**: No need to break existing CI configs / dev workflows. The env var is now effectively a no-op for UMD, but still documented as the "experimental vision" path.

### Decision 3: Test-fixture mode explicitly excludes UMD

When `TASKRUNNER_BUILD_MODE=test-fixture`, UMD code is **not** compiled. This is intentional — test-fixture is for users who only want the architecture/correctness tests, not the LD_PRELOAD shim.

**Rationale**: Preserves the test-fixture scope's identity (per AGENTS.md H-5 3-scope: test-fixture is currently-shippable, ACCEPTED; umd-evolution is experimental).

## Risks / Trade-offs

| # | Risk | Mitigation |
|---|------|------------|
| 1 | Default build size grows (UMD adds ~30 cu* symbols + shim) | Acceptable trade-off; the symbols are tiny and the UMD code already builds in umd-evolution mode |
| 2 | UMD shim becomes part of `libtaskrunner.a` (or equivalent) | Depends on existing CMake structure; may need to revisit library targets |
| 3 | Some test-fixture tests depend on UMD symbols being absent | Audit existing tests; should be no-op since UMD is already shipped in main |
| 4 | CI matrix breaks | Run all 3 modes (default, test-fixture, umd-evolution) locally before pushing |

## Verification

```bash
# Mode 1: default (no env var)
cmake -B build_default && cmake --build build_default -j4
ctest --test-dir build_default  # expect 318/318

# Mode 2: test-fixture opt-out
TASKRUNNER_BUILD_MODE=test-fixture cmake -B build_tf && cmake --build build_tf -j4
ctest --test-dir build_tf  # expect test-fixture tests only

# Mode 3: umd-evolution (backward compat alias)
TASKRUNNER_BUILD_MODE=umd-evolution cmake -B build_umd && cmake --build build_umd -j4
ctest --test-dir build_umd  # expect 318/318
```

## Open Questions

(none — exact change scope is mechanical CMake)
