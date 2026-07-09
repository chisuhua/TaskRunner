---
SCOPE: umd-evolution
STATUS: PROPOSED
---

# Tasks: umd-evolution-build-default-on

> **Goal**: Release the `TASKRUNNER_BUILD_MODE=umd-evolution` build-mode gate so UMD code is compiled by default.
> **Risk**: low (CMake change only, no source modifications).
> **Estimated effort**: 0.5-1 d.

## 1. Pre-flight: snapshot baseline

- [ ] 1.1 Verify current `cmake -B build -DTASKRUNNER_BUILD_MODE=umd-evolution` builds all 318 tests. This is the baseline that must continue to pass.
  ```bash
  cd /workspace/project/UsrLinuxEmu/external/TaskRunner
  cmake -B build_baseline -DTASKRUNNER_BUILD_MODE=umd-evolution
  cmake --build build_baseline -j4
  ctest --test-dir build_baseline  # expect 318/318
  ```

- [ ] 1.2 Verify current `cmake -B build` (no env var) does NOT build UMD:
  ```bash
  cmake -B build_default_check
  cmake --build build_default_check -j4
  ls build_default_check/src/umd/  # expect: directory does not exist
  ```

## 2. CMake change

- [ ] 2.1 Read current `CMakeLists.txt` to find the build-mode routing block (search for `TASKRUNNER_BUILD_MODE`).
- [ ] 2.2 Read current `cmake/UMDEvolution.cmake` to understand what it adds when invoked.
- [ ] 2.3 Modify the routing block: make UMD the default, `test-fixture` the opt-out.
  ```cmake
  # In CMakeLists.txt (or wherever the routing lives)
  if(TASKRUNNER_BUILD_MODE STREQUAL "test-fixture")
    add_subdirectory(src/test_fixture)
    add_subdirectory(tests/test_fixture)
  else()
    add_subdirectory(src/test_fixture)
    add_subdirectory(src/umd)
    add_subdirectory(tests/test_fixture)
    add_subdirectory(tests/umd)
  endif()
  ```
- [ ] 2.4 (Optional) If `cmake/UMDEvolution.cmake` is invoked conditionally, decouple it from the env var (always invoke; let the UMD sources be compiled unconditionally).

## 3. Verification (3-mode matrix)

- [ ] 3.1 Mode 1 (default):
  ```bash
  rm -rf build_default
  cmake -B build_default
  cmake --build build_default -j4
  ctest --test-dir build_default  # expect 318/318 PASS
  ```
- [ ] 3.2 Mode 2 (test-fixture opt-out):
  ```bash
  rm -rf build_tf
  TASKRUNNER_BUILD_MODE=test-fixture cmake -B build_tf
  cmake --build build_tf -j4
  ctest --test-dir build_tf  # expect test-fixture tests pass; UMD tests skipped
  ```
- [ ] 3.3 Mode 3 (umd-evolution alias, backward compat):
  ```bash
  rm -rf build_umd
  TASKRUNNER_BUILD_MODE=umd-evolution cmake -B build_umd
  cmake --build build_umd -j4
  ctest --test-dir build_umd  # expect 318/318 PASS (no regression)
  ```

## 4. Documentation

- [ ] 4.1 Update `AGENTS.md` build section to note: "UMD code is built by default. Use `TASKRUNNER_BUILD_MODE=test-fixture` to opt out."
- [ ] 4.2 Update `docs/umd-evolution/roadmap/current-status.md` to reflect that UMD is now default-on.
- [ ] 4.3 Run `tools/docs-audit.sh` and verify no new violations.

## 5. Commit + push

- [ ] 5.1 commit (atomic):
  ```bash
  git add CMakeLists.txt cmake/UMDEvolution.cmake AGENTS.md docs/umd-evolution/roadmap/current-status.md
  git commit -m "build(umd): release TASKRUNNER_BUILD_MODE gate, UMD default-on

  - Default cmake build now includes src/umd/ and tests/umd/
  - TASKRUNNER_BUILD_MODE=test-fixture explicitly excludes UMD (opt-out)
  - TASKRUNNER_BUILD_MODE=umd-evolution remains as backward-compat alias
  - All 318 existing tests pass in all 3 modes
  - Prerequisite for UMD-EVOLUTION → ACCEPTED promotion (entry 1/5)"
  ```
- [ ] 5.2 push:
  ```bash
  git push origin main
  ```
- [ ] 5.3 (Optional) Update UsrLinuxEmu submodule pointer if downstream CI depends on it:
  ```bash
  cd /workspace/project/UsrLinuxEmu
  git add external/TaskRunner
  git commit -m "chore(submodule): bump TaskRunner to <sha> (build-default-on)"
  git push origin main
  ```

## Acceptance Criteria

- 3-mode build matrix all pass
- 318 tests in default + umd-evolution modes
- test-fixture mode excludes UMD
- AGENTS.md and current-status.md updated
- docs-audit.sh passes
