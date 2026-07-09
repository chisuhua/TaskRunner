---
SCOPE: umd-evolution
STATUS: PROPOSED
---

## Why

The UsrLinuxEmu `phase4-sim-graph-launch-real-impl` change (merged 2026-07-09) made `cuGraphLaunch` truly async — fences are now signaled by `HardwarePullerEmu::handleComplete()` after batch completion, not immediately. A cross-repo coverage audit (`bg_40238db1` for test_fixture, `bg_70b42519` for umd) identified **3 critical test gaps** in TaskRunner's test suite that prevent proper validation of the new async behavior:

1. **BLOCKER (GAP-4a)**: `cuStreamSynchronize` in `src/umd/libcuda_shim/cu_stream.cpp` is a **complete no-op** — does not poll any fence. The async launch→sync cycle cannot be tested at all.

2. **HIGH (F-4 contract)**: The `fence_id >= (1ull << 32)` sim fence_id range is **never asserted** in `tests/umd/test_cu_graph.cpp` — only `call_count("submit_graph")` is checked, not the fence value returned by `submit_graph`.

3. **HIGH (sync-plan drift)**: `plans/sync-plan.md:244` claims `test_cu_graph_real` exists with 32/32 PASS, but the binary is **not defined** in `cmake/UMDEvolution.cmake`. The actual `test_cu_graph` is 30 cases (not 25 or 32). Documentation drift.

These gaps mean TaskRunner cannot validate the async fence lifecycle, only the synchronous shim logic.

## What Changes

- **Fix `cuStreamSynchronize`** (`src/umd/libcuda_shim/cu_stream.cpp`): make it actually wait on fences via the **existing** `g_gpu_client->wait_fence(fence_id, timeout_ms, &status)` overload in `include/shared/igpu_driver.hpp:228`. **No new interface method** — IGpuDriver already has two `wait_fence` overloads and all three implementations (GpuDriverClient, CudaStub, MockGpuDriver) already cover them.
- **Add a stream-local fence registry** (`src/umd/libcuda_shim/stream_fence_registry.hpp` new + `cu_graph.cpp` hook): `cuStreamSynchronize` needs to look up the most-recent fence for the stream. New internal-only header (UMD scope) provides `record_stream_fence(stream, fence_id)` / `get_stream_last_fence(stream)` with mutex protection.
- **Add F-4 contract assertion** (`tests/umd/test_cu_graph.cpp`): assert `fence_id >= (1ull << 32)` inside the existing "Launch records fence_id to LaunchTrace" test.
- **Extend MockGpuDriver** (`tests/test_fixture/mock_gpu_driver.hpp`) with non-breaking additive getters: `get_last_submit_graph_fence()`, `get_last_wait_fence_id()`, `get_wait_fence_call_count()`, `reset_fence_tracking()`. Required so the F-4 assertion can read the value `submit_graph` returned (currently only inputs are recorded, not the return value).
- **Add parameter verification** for `submit_graph` in two existing tests using the new getters.
- **Add 2 new TEST_CASE entries** in `tests/umd/test_cu_graph.cpp`:
  - `Launch + cuStreamSynchronize polls fence` (E2E async lifecycle)
  - `cuStreamSynchronize error propagation` (waits-fence error → `CUDA_ERROR_UNKNOWN`)
- **Update `plans/sync-plan.md`**: remove false `test_cu_graph_real` line; correct `test_cu_graph` count 25 → 30 → 32; recount Total.
- **Document the change properly**: `.openspec.yaml` fields completed; SCOPE/STATUS YAML front-matter added to this file.

## Capabilities

### New Capabilities

- `cu-graph-async-fence-testing`: End-to-end testing of cuGraphLaunch + cuStreamSynchronize async fence lifecycle (BLOCKER for Phase 4 verification).

### Modified Capabilities

(none — no requirement-level changes to existing specs.)

## Impact

- **Affected source files**:
  - `src/umd/libcuda_shim/cu_stream.cpp` (cuStreamSynchronize implementation fix + fence-registry backend)
  - `src/umd/libcuda_shim/cu_graph.cpp` (call `record_stream_fence` after successful submit_graph; add include for new internal header)
  - `src/umd/libcuda_shim/stream_fence_registry.hpp` (**new**, internal-only, UMD scope)
- **Affected test/mock files**:
  - `tests/test_fixture/mock_gpu_driver.hpp` (add 4 getters + save fence returns in `submit_graph` and `wait_fence` overrides)
  - `tests/umd/test_cu_graph.cpp` (add F-4 assertion in existing test + 2 new TEST_CASE entries)
- **Affected docs**:
  - `plans/sync-plan.md` (remove false `test_cu_graph_real` line; correct `test_cu_graph` count; recount Total)
  - `openspec/changes/test-cu-graph-coverage-fixes/.openspec.yaml` (status/scope/goal metadata)
- **Cross-repo dependency**: Follow-up to UsrLinuxEmu `phase4-sim-graph-launch-real-impl`. **No UsrLinuxEmu code changes required.** Step 6.7 (open tracking issue) + 6.8 (submodule pointer bump) remain as cross-repo notifications only.
- **Scopes touched**: **test-fixture** (mock getter additions) + **umd-evolution** (shim fix + new tests + new internal header). The previous draft's "UMD-evolution only" wording was inaccurate and is corrected here. **`shared` scope is NOT touched** (no IGpuDriver interface change required — `wait_fence` already exists).
- **No dual review required**: AGENTS.md mandates dual review only for `shared` scope changes. Since `include/shared/igpu_driver.hpp` is unmodified, normal review applies.
- **CI impact**: `test_cu_graph` UMD test count increases from **30 → 32** cases. The previous draft's "30 → 31" was a typo; correct count is +2 (1 E2E lifecycle + 1 error propagation).

## Notes on design decisions rejected by Metis+Oracle review

The first draft of this change proposed adding a third `wait_fence(uint64_t, uint64_t)` overload to `IGpuDriver`. That proposal was incorrect:

1. `IGpuDriver::wait_fence(uint64_t fence_id, uint32_t timeout_ms, uint32_t* status_out)` already exists (line 228).
2. All three implementations (GpuDriverClient header inline, CudaStub, MockGpuDriver) already override it.
3. Adding a third overload would force re-overrides in all three implementations, expand the vtable, and needlessly touch `shared` scope (triggering dual review).

The adopted approach reuses the existing 3-argument overload. See `design.md` §Decisions for the full rationale and alternatives considered.
