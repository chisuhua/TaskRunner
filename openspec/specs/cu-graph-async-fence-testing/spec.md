# cu-graph-async-fence-testing Specification

## Purpose
TBD - created by archiving change test-cu-graph-coverage-fixes. Update Purpose after archive.
## Requirements
### Requirement: cuStreamSynchronize must wait on the stream's most-recent fence

The `cuStreamSynchronize` shim function MUST poll the stream's most-recent
fence via `g_gpu_client->wait_fence(fence_id, timeout_ms, &status)` whenever
the stream has a recorded fence (i.e. a `cuGraphLaunch` has been issued
against it). The function MUST NOT remain a no-op.

The wait contract — per `include/shared/igpu_driver.hpp` `wait_fence` overload
(line 228) — is:

| `wait_fence` return | `*status` value | `cuStreamSynchronize` return |
|---------------------|-----------------|------------------------------|
| non-zero            | (don't care)    | `CUDA_ERROR_UNKNOWN`         |
| 0                   | 1               | `CUDA_SUCCESS`               |
| 0                   | 0               | `CUDA_ERROR_TIMEOUT` (defensive) |
| 0                   | -1 (0xFFFFFFFF) | `CUDA_ERROR_UNKNOWN`         |

Special-case shortcuts (with justification):

- `g_gpu_client == nullptr` AND a pending fence exists → `CUDA_ERROR_NOT_INITIALIZED`
  (consistent with `cuGraphLaunch` behavior on null driver).
- Stream has no recorded fence (`get_stream_last_fence(stream) == 0`) →
  `CUDA_SUCCESS` (backward compatibility for the 30 pre-existing tests that
  do not launch graphs).

#### Scenario: E2E async lifecycle — Launch then Synchronize polls wait_fence exactly once

- **WHEN** `cuGraphLaunch(exec, stream)` is called and returns `CUDA_SUCCESS`
  with the mock returning `(1ull << 32) + next_fence_id_` as fence
- **AND** `cuStreamSynchronize(stream)` is called immediately afterward
- **THEN** `cuStreamSynchronize` MUST return `CUDA_SUCCESS`
- **AND** `MockGpuDriver::get_wait_fence_call_count()` MUST equal 1
- **AND** `MockGpuDriver::get_last_wait_fence_id()` MUST equal the fence_id
  returned by the previous `submit_graph` call (sim fence range
  `>= (1ull << 32)`)

#### Scenario: cuStreamSynchronize returns NOT_INITIALIZED when driver is null and fence exists

- **WHEN** `g_gpu_client == nullptr` (set after a launch) and a fence has
  been recorded for the stream via the registry
- **AND** `cuStreamSynchronize(stream)` is called
- **THEN** the call MUST return `CUDA_ERROR_NOT_INITIALIZED` and MUST NOT
  invoke `wait_fence`

#### Scenario: cuStreamSynchronize returns SUCCESS for streams with no recorded fence

- **WHEN** `g_gpu_client` is set but no `cuGraphLaunch` has ever been issued
  against `stream` (registry lookup returns 0)
- **AND** `cuStreamSynchronize(stream)` is called
- **THEN** the call MUST return `CUDA_SUCCESS` and MUST NOT invoke `wait_fence`
  (covers all 30 pre-existing tests that never launch graphs)

#### Scenario: cuStreamSynchronize returns CUDA_ERROR_UNKNOWN when wait_fence fails

- **WHEN** `g_mock.set_canned_return("wait_fence", 1)` is active (non-zero = failure)
- **AND** `cuGraphLaunch(exec, stream)` has succeeded (fence recorded)
- **AND** `cuStreamSynchronize(stream)` is called
- **THEN** the call MUST return `CUDA_ERROR_UNKNOWN`

### Requirement: Stream-local fence registry bridges cu_graph.cpp to cu_stream.cpp

The system MUST provide an internal-only stream→fence registry accessible
from both `src/umd/libcuda_shim/cu_graph.cpp` (writer) and
`src/umd/libcuda_shim/cu_stream.cpp` (reader). The registry header MUST live
at `src/umd/libcuda_shim/stream_fence_registry.hpp` (NOT in `include/`,
because it is not a cross-scope public API).

The registry MUST expose:

```cpp
namespace async_task::umd::shim {
  void     record_stream_fence(void* stream, uint64_t fence_id);
  uint64_t get_stream_last_fence(void* stream);  // 0 if none
}
```

Storage MUST be `std::unordered_map<CUstream, uint64_t>` protected by
`std::mutex`. `CUstream` is `void*`, so `nullptr` is a legal key (used by
the existing "Launch with mock driver calls submit_graph once" test that
calls `cuGraphLaunch(exec, nullptr)`).

#### Scenario: record_stream_fence after successful submit_graph

- **WHEN** `cuGraphLaunch(exec, stream)` successfully calls
  `submit_graph(exec_handle, stream_id)` and receives a fence_id
- **THEN** `cu_graph.cpp` MUST invoke
  `record_stream_fence(hStream, static_cast<uint64_t>(fence))`
  immediately after the `submit_graph` success path (before returning
  `CUDA_SUCCESS`)

#### Scenario: get_stream_last_fence returns 0 for unknown streams

- **WHEN** `get_stream_last_fence(unknown_stream)` is called and `unknown_stream`
  has never had a fence recorded
- **THEN** the function MUST return 0 (not throw, not UB)

#### Scenario: concurrent record/get is thread-safe

- **WHEN** multiple threads call `record_stream_fence` and `get_stream_last_fence`
  concurrently
- **THEN** the internal `std::mutex` MUST serialize all access; no data race,
  no torn reads, no iterator invalidation

### Requirement: F-4 sim fence_id range contract is asserted

The `MockGpuDriver::submit_graph` implementation MUST return fences in the
sim fence range `>= (1ull << 32)`, computed as
`(1ull << 32) + next_fence_id_.fetch_add(1)`. Tests MUST assert this range
to lock down the F-4 contract documented in `tadr-301`.

#### Scenario: F-4 assertion in "Launch records fence_id to LaunchTrace" test

- **WHEN** `cuGraphLaunch(exec, stream)` returns `CUDA_SUCCESS`
- **THEN** the test MUST assert
  `g_mock.get_last_submit_graph_fence() >= static_cast<int64_t>(1ull << 32)`
- **AND** the test MUST also verify the previously-recorded
  `last_submit_graph_fence` was preserved (not overwritten by reset)

### Requirement: MockGpuDriver exposes non-breaking additive getters for fence + submit_graph param tracking

The `MockGpuDriver` MUST expose the following public getters, all `const`-correct
and additive (no existing public API removed or signature-changed):

```cpp
int64_t  get_last_submit_graph_fence()   const;  // -1 if never called
uint64_t get_last_submit_graph_exec()    const;  // 0 if never called
uint32_t get_last_submit_graph_stream()  const;  // 0 if never called
uint64_t get_last_wait_fence_id()        const;  // 0 if never called
size_t   get_wait_fence_call_count()     const;  // delegates to call_count("wait_fence")
void     reset_fence_tracking();                  // test isolation
```

Private state additions (all `mutable` for use from `const` getters):

```cpp
int64_t  last_submit_graph_fence_{-1};
uint64_t last_submit_graph_exec_{0};
uint32_t last_submit_graph_stream_{0};
uint64_t last_wait_fence_id_{0};
```

The `submit_graph` override MUST save the returned fence_id and both input
args before returning. The `wait_fence` 3-arg override MUST save the input
`fence_id` before delegating to `record(...)`.

#### Scenario: Existing tests using history()/call_count() are unaffected

- **WHEN** any pre-existing test (e.g. `test_gpu_phase2.cpp`,
  `test_cu_graph.cpp` non-Phase-4 tests) calls
  `mock.history()`, `mock.call_count("submit_graph")`, or iterates `CallRecord`
- **THEN** behavior MUST be identical to pre-change baseline; only new fields
  are added (no removal, no signature change)

#### Scenario: reset_fence_tracking clears all four state fields

- **WHEN** `reset_fence_tracking()` is called
- **THEN** `last_submit_graph_fence_` MUST be -1
- **AND** `last_submit_graph_exec_` MUST be 0
- **AND** `last_submit_graph_stream_` MUST be 0
- **AND** `last_wait_fence_id_` MUST be 0

### Requirement: submit_graph parameter verification in two existing tests

Two pre-existing `TEST_CASE` entries in `tests/umd/test_cu_graph.cpp` MUST
be enhanced with parameter verification using the new getters:

1. **"Launch with mock driver calls submit_graph once"** — after
   `cuGraphLaunch(exec, nullptr)` succeeds, assert:
   - `g_mock.get_last_submit_graph_exec() == static_cast<uint64_t>(exec)`
   - `g_mock.get_last_submit_graph_stream() == 0u` (CU_STREAM_LEGACY)

2. **"Launch records fence_id to LaunchTrace"** — after `cuGraphLaunch`
   succeeds, in addition to the existing LaunchTrace check, assert:
   - `g_mock.reset_fence_tracking()` (test isolation)
   - `g_mock.get_last_submit_graph_fence() >= static_cast<int64_t>(1ull << 32)`
     (F-4 contract)

#### Scenario: submit_graph parameter verification passes

- **WHEN** the two enhanced tests run with a fresh `MockGpuDriver` per test
  (no cross-test state leakage thanks to `reset_fence_tracking`)
- **THEN** all `CHECK`/`REQUIRE` assertions MUST pass; the test count remains
  at 32 (these are enhancements to existing cases, not new cases)

### Requirement: Two new TEST_CASE entries added for async fence lifecycle

The file `tests/umd/test_cu_graph.cpp` MUST contain exactly 32 `TEST_CASE`
entries (was 30 pre-change). The two new entries are:

1. **`cu_graph: Launch + cuStreamSynchronize polls fence`** — E2E async
   lifecycle: create stream + graph + exec, launch, assert fence range
   `>= (1ull << 32)`, synchronize, assert `wait_fence` was called exactly
   once with the correct fence_id, tear down.

2. **`cu_graph: cuStreamSynchronize error propagation`** — inject
   `wait_fence` failure via `g_mock.set_canned_return("wait_fence", 1)`,
   launch a graph, synchronize, assert `CUDA_ERROR_UNKNOWN`, reset the
   canned return, tear down.

#### Scenario: test_cu_graph runs 32 cases all green in umd-evolution build mode

- **WHEN** `ctest --test-dir build -R test_cu_graph --output-on-failure` runs
  after `cmake -B build -DTASKRUNNER_BUILD_MODE=umd-evolution && cmake --build build -j4`
- **THEN** the binary MUST execute 32 `TEST_CASE` blocks
- **AND** all 32 MUST report `PASS` (no `FAIL`, no `SKIP`)

### Requirement: sync-plan.md drift correction — remove false test_cu_graph_real, recount

The file `plans/sync-plan.md` MUST be corrected to remove a false reference
to a non-existent `test_cu_graph_real` binary and to recount the total test
count accurately:

- **REMOVE** the line referencing `test_cu_graph_real` (was at
  `plans/sync-plan.md:244`).
- **CORRECT** the `test_cu_graph` row from `25/25` to
  `30/30 (本 change 后 32/32)`.
- **RECOUNT** the `Total` row: subtract 32 (the false `test_cu_graph_real`
  reference) and add 7 (the net `test_cu_graph` growth from 25 → 32),
  yielding `245/245`.
- **NO** "270/270" footnote, "snapshot" annotation, or other rationalization
  that preserves the false reference.
- **ADD** this change's entry: `test-cu-graph-coverage-fixes (close async
  fence gap) — PROPOSED`.

#### Scenario: openspec validate + ctest confirm post-fix state

- **WHEN** `openspec validate test-cu-graph-coverage-fixes` runs after the
  fix
- **THEN** the sync-plan table MUST NOT contain `test_cu_graph_real`
- **AND** the `test_cu_graph` row MUST read `30/30 (本 change 后 32/32)`
- **AND** the `Total` row MUST read `245/245`

