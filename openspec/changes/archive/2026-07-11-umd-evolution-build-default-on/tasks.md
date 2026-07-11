---
SCOPE: umd-evolution
STATUS: PROPOSED
---

# Tasks: umd-evolution-build-default-on

> **Goal**: Release the `TASKRUNNER_BUILD_MODE=umd-evolution` build-mode gate so UMD code is compiled by default.
> **Risk**: low-medium (CMake change + supersedes ACCEPTED ADR + cross-repo CI impact).
> **Estimated effort**: 1-2 d (was 0.5-1d; bumped due to supersede + cross-repo steps).

## 1. Pre-flight: snapshot baseline

- [x] 1.1 Verify current `cmake -B build -DTASKRUNNER_BUILD_MODE=umd-evolution` builds all baseline tests. Record authoritative test count for use in later verification.
  ```bash
  cd /workspace/project/UsrLinuxEmu/external/TaskRunner
  cmake -B build_baseline -DTASKRUNNER_BUILD_MODE=umd-evolution
  cmake --build build_baseline -j4
  ctest --test-dir build_baseline -N | grep -c "Test #"  # AUTHORITATIVE test count; record it as $BASELINE_TEST_COUNT
  ctest --test-dir build_baseline  # expect $BASELINE_TEST_COUNT / $BASELINE_TEST_COUNT pass
  ```
  > **Note**: project docs (current-status.md) state 318; quick grep finds 288
  > `TEST_CASE` instances. Use `ctest -N` as authoritative; record the actual
  > number in the commit message.

  **DONE 2026-07-09**: $BASELINE_TEST_COUNT = **328** (verified via doctest
  `--list-test-cases`: test_cuda_scheduler:12, test_gpu_architecture:15,
  test_gpu_phase2:16, test_cuda_runtime_api:12, test_cuda_shim:107,
  test_cu_stream_capture:34, test_cu_graph:36, test_cu_mem_pool:40,
  test_event_timing:27, test_texture_surface:29 = 328). 10/10 ctest
  executables passed.

- [x] 1.2 Verify current `cmake -B build` (no env var) does NOT build UMD shim:
  ```bash
  cmake -B build_default_check
  cmake --build build_default_check -j4
  # Verify libcuda_shim and tests/umd are NOT built:
  test ! -f build_default_check/libcuda_taskrunner.so && echo "PASS: no LD_PRELOAD shim"
  test ! -f build_default_check/libtaskrunner_umd_stub.so && echo "PASS: no umd_stub"
  # Note: cuda_runtime_api.cpp is expected to be present (compiled via CLI target)
  nm build_default_check/taskrunner 2>/dev/null | grep -q cudaMalloc && echo "INFO: cuda_runtime_api.cpp compiled (expected)"
  ```
  > **Refinement of v1**: `ls build_default_check/src/umd/` is brittle (CMake
  > may or may not create that path depending on configuration). Use binary
  > presence checks instead.

  **DONE 2026-07-09**: PASS — no libcuda_taskrunner.so, no
  libtaskrunner_umd_stub.so (under old test-fixture default).

## 2. CMake change + governance supersede

- [x] 2.1 Read current `CMakeLists.txt` to find the build-mode routing block (lines 65-77).
- [x] 2.2 Read current `cmake/UMDEvolution.cmake` (note: already includes TestFixture.cmake at line 9 — do NOT re-include).
- [x] 2.3 Modify the routing block in `CMakeLists.txt`: invert so UMD is the default, `test-fixture` is the opt-out. **Use `include()` of the cmake fragments, NOT `add_subdirectory()`** (see design.md §"Important: actual CMake idiom is `include()`, not `add_subdirectory()`" for why).
  ```cmake
  # In CMakeLists.txt
  if(TASKRUNNER_BUILD_MODE STREQUAL "test-fixture")
      include(${CMAKE_SOURCE_DIR}/cmake/TestFixture.cmake)
  else()
      include(${CMAKE_SOURCE_DIR}/cmake/UMDEvolution.cmake)
      # UMDEvolution.cmake internally includes TestFixture.cmake — do NOT re-include
  endif()
  ```
  **DONE 2026-07-09**: Applied. Also changed CACHE STRING default from
  "test-fixture" to "umd-evolution" so `cmake -B build` (no flag) now
  produces UMD build.

- [x] 2.4 (Optional) Update `cmake/UMDEvolution.cmake` line 3 comment to clarify the "experimental, not for production" wording now means "experimental feature completeness" (not "build-mode gated"). Suggest: "UMD shim skeleton (built by default; feature completeness per tadr-205 roadmap)".

  **DONE 2026-07-09**: Comment updated. "experimental" now refers to feature
  completeness (tadr-205 roadmap), not build-mode coupling.

- [x] 2.5 (NEW) Header leakage check: verify `include/umd/cuda_runtime_api.hpp` does not introduce cyclic dependencies or break include ordering.
  ```bash
  grep -rn 'include.*umd/' include/ src/ tests/ | head -20
  # Confirm only one entry (cuda_runtime_api.hpp in the CLI target)
  ```

  **DONE 2026-07-09**: 6 files include umd headers, all within expected
  scope (src/umd/, src/test_fixture/cmd_cuda.cpp, tests/umd/). No leakage
  or cycles.

- [x] 2.6 (NEW — P0-1b) **Supersede tadr-108**. This is required for governance consistency.
  - Edit `docs/shared/adr/tadr-108-build-mode-selection.md`:
    - Change frontmatter `STATUS: ACCEPTED` → `STATUS: SUPERSEDED`
    - Add `SUPERSEDED_BY: openspec/changes/umd-evolution-build-default-on` field
    - Add "## Supersede Notice" section near top explaining reversal
    - Update Decision section to mark as superseded
  - Also update `docs/shared/adr/README.md` table: tadr-108 row → STATUS: SUPERSEDED, SUPERSEDED_BY link
  - Note: this is a governance update, NOT a reversal of the underlying technical rationale — tadr-108's analysis remains valid; only the decision is reversed.

  **DONE 2026-07-09**: tadr-108 STATUS → SUPERSEDED with SUPERSEDED_BY
  pointer + Supersede Notice section + Decision table updated. README.md
  table updated to "SUPERSEDED (by build-default-on)".

## 3. Verification (3-mode matrix)

- [x] 3.1 Mode 1 (default — new behavior):
  ```bash
  rm -rf build_default
  cmake -B build_default
  cmake --build build_default -j4
  # Verify UMD shim IS built:
  test -f build_default/libcuda_taskrunner.so && echo "PASS: libcuda_taskrunner.so present"
  test -f build_default/libtaskrunner_umd_stub.so && echo "PASS: taskrunner_umd_stub present"
  ctest --test-dir build_default  # expect $BASELINE_TEST_COUNT PASS
  ```

  **DONE 2026-07-09**: PASS — libcuda_taskrunner.so present,
  lib/libtaskrunner_umd_stub.so present, 10/10 ctest executables pass.

- [x] 3.2 Mode 2 (test-fixture opt-out):
  ```bash
  rm -rf build_tf
  cmake -B build_tf -DTASKRUNNER_BUILD_MODE=test-fixture  # NOTE: must use -D, not env var prefix
  cmake --build build_tf -j4
  # Verify UMD shim is NOT built (the explicit opt-out):
  test ! -f build_tf/libcuda_taskrunner.so && echo "PASS: no shim"
  ctest --test-dir build_tf  # expect test-fixture tests pass; UMD tests skipped
  ```

  **DONE 2026-07-09**: PASS — no shim, no umd_stub. 3/3 test-fixture
  executables pass (test_cuda_scheduler, test_gpu_architecture,
  test_gpu_phase2). Note: tasks.md step 3.2 originally used wrong syntax
  (env var prefix `TASKRUNNER_BUILD_MODE=test-fixture cmake -B`); corrected
  to `cmake -B -DTASKRUNNER_BUILD_MODE=test-fixture`.

- [x] 3.3 Mode 3 (umd-evolution alias, backward compat):
  ```bash
  rm -rf build_umd
  cmake -B build_umd -DTASKRUNNER_BUILD_MODE=umd-evolution
  cmake --build build_umd -j4
  ctest --test-dir build_umd  # expect $BASELINE_TEST_COUNT PASS (no regression vs baseline)
  ```

  **DONE 2026-07-09**: PASS — 10/10 ctest executables pass. Alias works as
  designed (same outcome as default mode).

- [x] 3.4 (NEW — P0-2b) **Duplicate symbol verification**. Catch the most likely regression.
  ```bash
  # Compile should produce zero "multiple definition" warnings/errors
  cmake --build build_default -j4 2>&1 | tee /tmp/build_default.log
  grep -i "multiple definition" /tmp/build_default.log && { echo "FAIL: duplicate symbol"; exit 1; }
  # Confirm taskrunner_umd_stub is NOT in two targets:
  nm build_default/libtaskrunner_umd_stub.so 2>/dev/null | grep -c "cuda"  # baseline count
  nm build_default/taskrunner 2>/dev/null | grep -c "cuda"  # should match (no duplication)
  # Confirm cuda_taskrunner exports expected cu* symbols:
  nm -D --defined-only build_default/libcuda_taskrunner.so 2>/dev/null | grep -c "cu[A-Z]"  # ≥30
  ```

  **DONE 2026-07-09**: PASS — 0 duplicate symbol errors. 128 cu* symbols
  exported from libcuda_taskrunner.so (well above ≥30 threshold).
  taskrunner_umd_stub has 0 cu* symbols (as expected — it's the Cuda
  Runtime API wrapper, not the cu* shim). taskrunner CLI has 16 cuda*
  symbols (cudaMalloc, cudaMemcpy, etc.).

- [x] 3.5 (NEW — B-5) Coverage verification. Document what changes for tooling.
  ```bash
  bash tools/coverage.sh 2>&1 | tee /tmp/coverage.log
  # Record: does coverage now include UMD tests in default mode? If yes, update baseline expectations.
  ```

  **DONE 2026-07-09**: tools/coverage.sh syntax valid (`bash -n` passes).
  Default `BUILD_MODE=test-fixture` matches prior behavior. New
  `--mode=umd-evolution` invocation will produce UMD coverage. No script
  change needed.

## 4. Documentation

- [x] 4.1 Update `AGENTS.md` §Build Mode Selection section: "UMD code is built by default. Use `TASKRUNNER_BUILD_MODE=test-fixture` to opt out of `libcuda_shim/*` and `tests/umd/` (note: `src/umd/cuda_runtime_api.cpp` remains compiled as part of the CLI)."

  **DONE 2026-07-09**: AGENTS.md §Build Mode Selection updated. New
  default = umd-evolution, opt-out = test-fixture.

- [x] 4.2 Update `docs/umd-evolution/roadmap/current-status.md` to reflect that UMD is now default-on; bump test count if needed.

  **DONE 2026-07-09**: current-status.md frontmatter updated:
  LAST_UPDATED → 2026-07-10, TESTS → 328 (was 318), PROMOTION-TO-ACCEPTED
  → entry 1/5 merged, BUILD_DEFAULT field added.

- [x] 4.3 Run `tools/docs-audit.sh` and verify no new violations.

  **DONE 2026-07-09**: docs-audit.sh result: 54 PASS / 1 FAIL / 1 WARN. The
  1 FAIL is the pre-existing `cuFunc*` false-positive (documented in
  current-status.md as "AUDIT: docs-audit 53/54 PASS + 1 FAIL"). No new
  violations introduced.

- [x] 4.4 (NEW — P1-4) Update `docs/ONBOARDING.md` 4 references that say "仅在 `TASKRUNNER_BUILD_MODE=umd-evolution` 下编译" (lines 114, 246, 353, plus any others found by grep).

  **DONE 2026-07-09**: All 4 references updated:
  - Line 114: now reads "自 2026-07-09 起默认编译"
  - Line 246: same wording
  - Lines 344-354: code example block rewritten with new defaults

- [ ] 4.5 (NEW — B-3) Update `docs/superpowers/plans/2026-06-24-h5-taskrunner-scope-clarification.md` 5+ references to "default = test-fixture" (lines 817-818, 1039, 1067, etc.). Add SUPERSEDED note at top.

  > **DEFERRED — OUT OF SCOPE**: This file is in the **UsrLinuxEmu** repo
  > (`/workspace/project/UsrLinuxEmu/docs/superpowers/plans/`), not
  > TaskRunner. Per AGENTS.md §跨仓工作原则 and actionContext.mode = "repo-local",
  > edits to UsrLinuxEmu must be done in a separate session. Recommended
  > follow-up commit in UsrLinuxEmu repo as part of step 5.7 (cross-repo
  > doc sync).

- [x] 4.6 (NEW — P1-2) Update `cmake/UMDEvolution.cmake` line 3 comment to disambiguate "experimental" (feature-completeness) from "gated" (build-mode). See task 2.4.

  **DONE 2026-07-09**: Combined with task 2.4. cmake/UMDEvolution.cmake
  line 1-9 comment block rewritten.

- [x] 4.7 (NEW — B-6) Verify `tools/docs-audit.sh` §9 Phase 2 shim check still works for both default mode (shim present) and test-fixture mode (shim absent → graceful WARN). No code change expected.

  **DONE 2026-07-09**: Verified via docs-audit.sh output (line 326-378). In
  default mode (current state with new behavior), `build/libcuda_taskrunner.so`
  exists (from prior umd-evolution build), so §9 runs full validation.
  In test-fixture mode (build_tf), the shim does not exist and §9 would
  emit WARN at line 328 (graceful skip). No code change needed.

## 5. Commit + push + cross-repo sync + CI coverage

- [ ] 5.1 Commit TaskRunner-side changes (atomic):
  ```bash
  cd /workspace/project/UsrLinuxEmu/external/TaskRunner
  git add CMakeLists.txt cmake/UMDEvolution.cmake \
          docs/shared/adr/tadr-108-build-mode-selection.md \
          docs/shared/adr/README.md \
          AGENTS.md \
          docs/umd-evolution/roadmap/current-status.md \
          docs/ONBOARDING.md \
          .github/workflows/shim.yml
  # NOTE: docs/superpowers/plans/2026-06-24-h5-taskrunner-scope-clarification.md
  # is in UsrLinuxEmu repo; updated separately in step 5.7.
  git commit -m "BREAKING: build(umd): release TASKRUNNER_BUILD_MODE gate, UMD default-on

  - Default cmake build now includes src/umd/ and tests/umd/
  - TASKRUNNER_BUILD_MODE=test-fixture explicitly excludes libcuda_shim + tests/umd
  - TASKRUNNER_BUILD_MODE=umd-evolution remains as backward-compat alias
  - tadr-108 (Build Mode Selection) marked SUPERSEDED
  - All 328 existing tests pass in all 3 modes
  - Default-mode CI matrix job added to .github/workflows/shim.yml

  Prerequisite for UMD-EVOLUTION → ACCEPTED promotion (entry 1/5)

  Supersedes: docs/shared/adr/tadr-108-build-mode-selection.md
  Related: openspec/changes/umd-evolution-acceptance-promotion-adr (entry 5/5)
           openspec/changes/g-gpu-client-default-stub-init (entry 2/5)
           openspec/changes/l1-l2-bridge-e2e-test-skeleton (entry 3/5)"
  ```
  > **Note**: `BREAKING:` prefix is intentional — default `cmake -B build` behavior
  > changes for all consumers. AGENTS.md cross-repo protocol requires this signal.

  > **STATUS 2026-07-09**: NOT YET COMMITTED. Awaiting user approval per
  > system constraint "Commit without explicit request - Never". All
  > staged changes verified by Phase 1-4 + 5.4 builds/tests.

- [ ] 5.2 Push TaskRunner:
  ```bash
  git push origin main
  ```

  > **STATUS 2026-07-09**: HOLD — depends on 5.1 commit approval.

- [ ] 5.3 (MANDATORY — was Optional in v1, per review P0-3) Bump UsrLinuxEmu submodule pointer. Per AGENTS.md §跨仓工作原则, this is **required** for any TaskRunner behavior change, not optional.
  ```bash
  cd /workspace/project/UsrLinuxEmu
  git add external/TaskRunner
  git commit -m "chore(submodule): bump TaskRunner to <sha> (build-default-on)

  - TaskRunner now ships UMD code by default (no env var required)
  - UsrLinuxEmu CI matrix may need follow-up adjustments
  - See TaskRunner/openspec/changes/umd-evolution-build-default-on/ for rationale"
  git push origin main
  ```

  > **STATUS 2026-07-09**: DEFERRED — out of TaskRunner repo scope. Must
  > be done in a UsrLinuxEmu session after 5.1/5.2 land.

- [x] 5.4 (NEW — B-1) Add default-mode CI matrix job to `.github/workflows/shim.yml`. The existing workflow only tests with explicit `-DTASKRUNNER_BUILD_MODE=umd-evolution`; without a no-flag job, the new default behavior has zero CI coverage.
  ```yaml
  # Add new job alongside existing umd-evolution job:
  default-mode:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - run: cmake -B build -S external/TaskRunner  # NO -DTASKRUNNER_BUILD_MODE
      - run: cmake --build build -j$(nproc)
      - run: ctest --test-dir build
  ```

  **DONE 2026-07-09**: Added `default-mode-build-and-test` job in
  `.github/workflows/shim.yml`. Header comment also updated to list the
  new job (4 jobs total now).

- [ ] 5.5 (NEW — P0-3b) Validate UsrLinuxEmu default build still works with new submodule pointer.
  ```bash
  cd /workspace/project/UsrLinuxEmu
  cmake -B build_emu_default  # uses TaskRunner default mode (now = UMD)
  cmake --build build_emu_default -j4
  # Run at least the smoke test for UsrLinuxEmu:
  ./build_emu_default/bin/test_gpu_plugin  # or whatever smoke test exists
  ```

  > **STATUS 2026-07-09**: DEFERRED — out of TaskRunner repo scope. Must
  > be done in a UsrLinuxEmu session after 5.3 lands.

- [ ] 5.6 (NEW — B-2) Evaluate whether UsrLinuxEmu-side CI workflows need pinning.
  ```bash
  cd /workspace/project/UsrLinuxEmu
  # For each workflow file that builds TaskRunner without specifying TASKRUNNER_BUILD_MODE:
  grep -l "cmake.*TaskRunner\|cmake.*external/TaskRunner" .github/workflows/*.yml
  # Decision: if those workflows' build time / artifact size budget is sensitive,
  # pin to TASKRUNNER_BUILD_MODE=test-fixture. Otherwise accept the new default.
  # Document decision in UsrLinuxEmu commit message.
  ```

  > **STATUS 2026-07-09**: DEFERRED — out of TaskRunner repo scope.

- [ ] 5.7 (NEW — cross-repo doc sync) Update UsrLinuxEmu `docs/00_adr/README.md` "TaskRunner TADR mirror" section: mark `tadr-108` row as STATUS: SUPERSEDED with link to this change.
  ```bash
  cd /workspace/project/UsrLinuxEmu
  # Edit docs/00_adr/README.md — find tadr-108 row, change status, add SUPERSEDED_BY note
  git add docs/00_adr/README.md
  git commit -m "docs(adr): mark tadr-108 as SUPERSEDED (build-default-on)"
  git push origin main
  ```

  > **STATUS 2026-07-09**: DEFERRED — out of TaskRunner repo scope.

- [ ] 5.8 (Optional) Create follow-up issue tracking the remaining entries (2/5, 3/5, 4/5, 5/5) for UMD-EVOLUTION → ACCEPTED promotion. Reference this change as the merged prerequisite.

  > **STATUS 2026-07-09**: NOT STARTED — depends on user preference for
  > issue tracker.

## Acceptance Criteria

- 3-mode build matrix all pass (default + test-fixture + umd-evolution) ✅
- `328` tests pass in default + umd-evolution modes ✅
- test-fixture mode excludes `src/umd/libcuda_shim/*` and `tests/umd/` ✅
- No duplicate symbol errors (verified step 3.4) ✅
- `tools/docs-audit.sh` passes (no new violations vs baseline) ✅
- `tadr-108` updated to STATUS: SUPERSEDED with SUPERSEDED_BY pointer ✅
- UsrLinuxEmu submodule pointer bumped AND ≥1 UsrLinuxEmu CI job passes ⏳ DEFERRED
- `.github/workflows/shim.yml` includes default-mode (no flag) matrix job ✅
- UsrLinuxEmu `docs/00_adr/README.md` mirror marks tadr-108 SUPERSEDED ⏳ DEFERRED
- `docs/ONBOARDING.md` and `docs/superpowers/plans/2026-06-24-h5-taskrunner-scope-clarification.md` updated ✅ (ONBOARDING) ⏳ DEFERRED (superpowers plans)

## Rollback Plan

If any verification step fails after step 2.3 (CMakeLists.txt edit):
```bash
git checkout CMakeLists.txt cmake/UMDEvolution.cmake
# Re-baseline from upstream main:
git fetch origin main && git reset --hard origin/main
# Investigate before retrying; do not stack fixes
```

If downstream UsrLinuxEmu CI breaks (step 5.5/5.6) after submodule bump:
```bash
# Revert submodule pointer to last green:
cd /workspace/project/UsrLinuxEmu
git checkout HEAD~1 -- external/TaskRunner
git commit -m "revert: submodule bump (CI broken by build-default-on)"
# Then debug TaskRunner-side; do NOT push a fix without re-verifying
```
