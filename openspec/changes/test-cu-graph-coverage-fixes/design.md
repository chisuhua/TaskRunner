---
SCOPE: umd-evolution
STATUS: PROPOSED
---

## Context

The UsrLinuxEmu `phase4-sim-graph-launch-real-impl` change made `cuGraphLaunch` truly async — `HardwarePullerEmu::handleComplete()` signals the sim fence after batch completion. TaskRunner's `cuStreamSynchronize` was previously a complete no-op, so the async launch→sync cycle could not be validated in TaskRunner's test suite. This design fixes the no-op, introduces a small internal stream-local fence registry, and adds coverage for the new async behavior.

**Current state**: TaskRunner umd ctest 30/30 PASS in `test_cu_graph`, but `cuStreamSynchronize` does not actually wait.

**Target state**: `test_cu_graph` 32/32 PASS, `cuStreamSynchronize` correctly polls the most-recent fence for the stream and returns per the contract below. No IGpuDriver interface change. No `shared` scope change. Test-fixture scope gains 4 non-breaking getters on `MockGpuDriver`.

## Goals / Non-Goals

**Goals:**
- Fix `cuStreamSynchronize` to actually poll fences (BLOCKER for async test coverage)
- Add F-4 contract assertion: `fence_id >= (1ull << 32)` for sim fence_ids
- Add parameter verification for `submit_graph` (exec_handle, stream_id)
- Update `sync-plan.md` to reflect actual test count (30 → 32, not 32 → 32)
- Add 2 new E2E tests: async lifecycle + wait_fence error propagation

**Non-Goals:**
- **Changing the IGpuDriver interface** — the existing `wait_fence(uint64_t, uint32_t, uint32_t*)` overload at `include/shared/igpu_driver.hpp:228` is reused. No new virtual methods, no vtable changes.
- Performance/benchmark work
- Modifying UsrLinuxEmu side (already complete in `phase4-sim-graph-launch-real-impl`)
- Adding CLI `cuda_graph_*` commands (future independent change)
- Tracking all pending fences per stream (we only track the latest; matches the simplified TestRunner scope — see Decision 2)

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│ umd-evolution: src/umd/libcuda_shim/                                │
│                                                                     │
│  ┌─────────────────────────┐      ┌──────────────────────────────┐  │
│  │ cu_graph.cpp            │      │ cu_stream.cpp                │  │
│  │                         │      │                              │  │
│  │ cuGraphLaunch()         │      │ cuStreamSynchronize()        │  │
│  │   submit_graph ─┐       │      │   g_gpu_client->wait_fence() │  │
│  │   fence_id ◄────┘       │      │             ▲                │  │
│  │   │                     │      │             │                │  │
│  │   ▼                     │      │   get_stream_last_fence()    │  │
│  │   record_stream_fence() ─────► │                              │  │
│  │                         │      │                              │  │
│  └─────────────────────────┘      └──────────────────────────────┘  │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │ stream_fence_registry.hpp  (NEW, internal-only)              │   │
│  │                                                              │   │
│  │ namespace async_task::umd::shim {                            │   │
│  │   void     record_stream_fence(void* stream, uint64_t fence); │   │
│  │   uint64_t get_stream_last_fence(void* stream);              │   │
│  │ }                                                             │   │
│  │ impl: unordered_map<CUstream, uint64_t> + std::mutex         │   │
│  └──────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│ test-fixture: tests/test_fixture/mock_gpu_driver.hpp                │
│   + private: last_submit_graph_fence_ (int64_t)                     │
│   + private: last_wait_fence_id_ (uint64_t)                         │
│   + submit_graph() saves return value                               │
│   + wait_fence() saves fence_id arg                                 │
│   + public getters: get_last_submit_graph_fence()                   │
│                     get_last_wait_fence_id()                        │
│                     get_wait_fence_call_count()                     │
│                     reset_fence_tracking()                          │
└─────────────────────────────────────────────────────────────────────┘

include/shared/igpu_driver.hpp  ←  NOT MODIFIED  (existing wait_fence reused)
```

## Decisions

### Decision 1: Reuse the existing `wait_fence` overload — **do NOT add a new one**

**Rationale**: `include/shared/igpu_driver.hpp:228` already declares
```cpp
virtual int wait_fence(uint64_t fence_id, uint32_t timeout_ms, uint32_t* status_out) = 0;
```
with a second 1-arg overload on line 232. All three implementations (`GpuDriverClient` inline header, `CudaStub`, `MockGpuDriver`) already override both. The previous change-draft proposed adding a third overload `wait_fence(uint64_t, uint64_t)`. That proposal was incorrect — it would have:
1. Required re-overrides in all three implementations (growing the vtable)
2. Crossed into `shared` scope (triggering dual review per AGENTS.md)
3. Been unnecessary, since `cuStreamSynchronize` only needs what the existing overload provides

**Reuse contract**:
- `cuStreamSynchronize` calls `g_gpu_client->wait_fence(fence_id, 0 /* infinite */, &status)`
- `*status` semantics (per `IGpuDriver` docstring): `1=signaled`, `0=timeout`, `-1=error`
- Map `status==1` → `CUDA_SUCCESS`; `status==0` → `CUDA_ERROR_TIMEOUT`; `status==-1` → `CUDA_ERROR_UNKNOWN`; `wait_fence` returns non-zero → `CUDA_ERROR_UNKNOWN`

**Alternatives considered (all rejected):**
- Add a new `(uint64_t, uint64_t)` overload — rejected, see rationale above.
- Direct `GPU_IOCTL_WAIT_FENCE` ioctl from the shim — rejected, violates the IGpuDriver abstraction boundary (magic='G' ioctls must go through `g_gpu_client`).
- Skip `wait_fence` and have cuStreamSynchronize inspect LaunchTrace directly — rejected, LaunchTrace is keyed by CUgraphExec not by stream and lives in cu_graph.cpp's anonymous namespace.

### Decision 2: Stream tracks the most recent fence (not all pending fences)

**Rationale**: Real CUDA's `cuStreamSynchronize` waits for **all** pending operations on the stream. For TaskRunner's test scope, we model a simplified contract: each `cuGraphLaunch` updates the stream's "current fence" to the one returned by `submit_graph`. `cuStreamSynchronize` then waits on that single fence. This is sufficient for the test scenarios (single-graph launch → sync); a future Phase can extend to multi-fence tracking if real multi-launch async scenarios are added.

**Alternatives considered:**
- Track list of all pending fences per stream — rejected, overkill for current scope; revisit when Phase 4 multi-launch scenarios are added.
- Track only the latest fence per stream — **chosen**, matches Phase 4 test scope.

### Decision 3: New internal-only header `stream_fence_registry.hpp` (in `src/`, not `include/`)

**Rationale**: `cu_graph.cpp` writes the fence (after successful `submit_graph`); `cu_stream.cpp` reads it (in `cuStreamSynchronize`). Two TUs in the same shared library need to share state. Three options:

(a) **Both include a new internal header** — chosen. Cleanest, no forward declaration drift, type-safe.
(b) `extern` declaration in one TU — feasible but error-prone if signatures change.
(c) Reuse the existing `LaunchTrace` and key it by stream — rejected, LaunchTrace is keyed by CUgraphExec; cross-domain coupling.

The header lives at `src/umd/libcuda_shim/stream_fence_registry.hpp` (NOT in `include/`) because:
- It's not part of any public API consumed by other scopes
- AGENTS.md requires `include/` only for cross-scope public headers
- Keeping it internal signals "no outside callers" via path convention

**Storage**: `std::unordered_map<CUstream, uint64_t>` protected by `std::mutex`. `CUstream` is a `void*` so `nullptr` is a legal key (used by the existing "Launch with mock driver calls submit_graph once" test that calls `cuGraphLaunch(exec, nullptr)`).

### Decision 4: MockGpuDriver extends with 4 non-breaking getters; **NO** CallRecord schema change

**Rationale**: The F-4 assertion needs the value `submit_graph` **returned**, not its arguments. The existing `CallRecord` records only input args, so we add three new private fields + four public getters. This is an additive change that does not affect any existing test that uses `history()` or `call_count()`.

**Additions to MockGpuDriver** (all additive, all `const`-correct):
```cpp
int64_t  get_last_submit_graph_fence() const;  // -1 if never called
uint64_t get_last_wait_fence_id() const;       // 0 if never called
size_t   get_wait_fence_call_count() const;    // delegates to call_count("wait_fence")
void     reset_fence_tracking();                // test isolation
```

Private state:
```cpp
int64_t  last_submit_graph_fence_{-1};
uint64_t last_wait_fence_id_{0};
```

`MockGpuDriver::submit_graph` override body, after computing `ret`, adds `last_submit_graph_fence_ = ret;` before returning.
`MockGpuDriver::wait_fence` 3-arg override body, after `record(...)`, adds `last_wait_fence_id_ = fence_id;` before returning.

**Alternatives considered (all rejected):**
- Extend `CallRecord` with a `ret_value` field — rejected, breaks every existing test that iterates `history()` (every record would now carry an unused field); also pollutes the schema for the one method that needs it.
- Compute fence from `MockGpuDriver::next_fence_id_` — rejected, that field is private and incremented post-call; we'd need to add a getter too, equivalent in cost.
- Expose `LaunchTrace` from cu_graph.cpp to tests — rejected, couples tests to internal state of the production shim.

### Decision 5: `canned_int("wait_fence", ...)` is the error-injection path

**Rationale**: `MockGpuDriver::wait_fence` already returns `canned_int("wait_fence", 0)`. The 1-arg version uses `canned_int("wait_fence_1arg", 0)`. Tests use `g_mock.set_canned_return("wait_fence", <non-zero>)` to inject failure. Note the actual `canned_i32` field name proposed in the first tasks-draft does not exist; the real mechanism is `canned_u64_["wait_fence"]` (lookup returns `static_cast<int>(value)`).

**Why mock returns SUCCESS immediately**: The mock represents the GPU backend; in tests we control fence state via `submit_graph`'s return value and `canned_int` injection. Real Puller timing is UsrLinuxEmu's responsibility (the `HardwarePullerEmu::handleComplete()` real implementation already lives on the UsrLinuxEmu side).

### Decision 6: `sync-plan.md` update — remove `test_cu_graph_real`, recount `test_cu_graph`

**Rationale**: `plans/sync-plan.md:244` falsely references a `test_cu_graph_real` binary with 32/32 PASS. `cmake/UMDEvolution.cmake:81-85` only defines `test_cu_graph` from `tests/umd/test_cu_graph.cpp`. The 6 Phase 4 tests already live in `test_cu_graph` (30 total). The 25 → 30 → 32 progression should be reflected. Removing the false reference is the simplest correct action.

**Order of edits**:
1. Delete the `test_cu_graph_real` line entirely
2. Change `test_cu_graph` row from `25/25` to `30/30 (本次 change 后 32/32)`
3. Recount the `Total` row, or annotate that the table reflects a snapshot and that subsequent Phase 3+ growth should be tracked separately
4. Add this change's row

**Alternatives considered (all rejected):**
- Create the missing `test_cu_graph_real` binary — rejected, splits an already-consolidated test suite.
- Add a "TODO: future cleanup" note — rejected, future readers get confused.

### Decision 7: NO CLI `cuda_graph_*` commands in this change

**Rationale**: `src/test_fixture/cmd_cuda.cpp` has no graph-related commands. Adding them would:
1. Expand scope into test-fixture's CLI subsystem
2. Require real GpuDriverClient integration tests (not mock-friendly)
3. Be unrelated to the three coverage gaps we are closing

These belong in a future "Phase 4 CLI graph commands" change, tracked separately in `docs/umd-evolution/roadmap/` (if such roadmap exists) or as a fresh OpenSpec change.

## cuStreamSynchronize Contract

| Condition | Return value | Rationale |
|-----------|--------------|-----------|
| `g_gpu_client == nullptr` | `CUDA_ERROR_NOT_INITIALIZED` | Consistent with `cuGraphLaunch` returning the same when `g_gpu_client` is null. |
| stream has no pending fence (`fence_id == 0`) | `CUDA_SUCCESS` | Backward compatibility: 30 existing tests that don't launch graphs must continue to pass. |
| `wait_fence` returns 0, `*status == 1` | `CUDA_SUCCESS` | Fence signaled. |
| `wait_fence` returns 0, `*status == 0` | `CUDA_ERROR_TIMEOUT` | Should not occur in practice (timeout=0 means infinite wait); defensive. |
| `wait_fence` returns 0, `*status == -1` | `CUDA_ERROR_UNKNOWN` | Generic error mapping. |
| `wait_fence` returns non-zero | `CUDA_ERROR_UNKNOWN` | ioctl failure, cannot precisely map errno→CUresult. |

## Risks / Trade-offs

| # | Risk | Mitigation |
|---|------|------------|
| 1 | `cuStreamSynchronize` fix may break existing umd tests that relied on the no-op behavior. | Run full umd ctest after change; all 30 pre-existing tests should still pass because `fence_id == 0` → SUCCESS path covers "no graph launched" scenarios. |
| 2 | New async lifecycle test may be flaky due to mock timing. | Mock returns synchronously; the only non-determinism is `next_fence_id_` increment which is atomic. Use `reset_fence_tracking()` between tests. |
| 3 | `fence_id >= 1ull << 32` assertion may be too strict if HAL fence range is later introduced in TaskRunner. | Assert the range based on the current IGpuDriver contract; revisit when HAL fences are added to TaskRunner scope. |
| 4 | `std::unordered_map<CUstream, uint64_t>` lookup in cuStreamSynchronize adds lock contention. | Single mutex; UMD shim is single-threaded in test paths; production usage is bottlenecked by GPU ioctls that already dwarf a map lookup. |
| 5 | Stream-local registry may grow unbounded if streams aren't destroyed. | Out of scope for this change; tracked under Phase 5 "stream lifecycle cleanup" candidate. |
| 6 | Cross-repo notification (issue + submodule bump) is forgotten. | Step 5.7 + 5.8 of `tasks.md` are explicit checklist items, not "if time permits". |

## Migration Plan

The 8-step TDD sequence (full detail in `tasks.md`):

1. MockGpuDriver getters added + F-4 assertion in existing test
2. MockGpuDriver getters implemented (save fence returns)
3. New TEST_CASE `Launch + cuStreamSynchronize polls fence` added (will fail because cuStreamSynchronize is still no-op)
4. `stream_fence_registry.hpp` created + implementation in `cu_stream.cpp`
5. `cu_graph.cpp` inserts `record_stream_fence(...)` call after successful submit_graph
6. `cuStreamSynchronize` replaced with the contract table implementation; step 3 now passes
7. New TEST_CASE `cuStreamSynchronize error propagation` added
8. `.openspec.yaml` metadata, `plans/sync-plan.md` drift fix, SCOPE/STATUS YAML in this file; full ctest verification; cross-repo notifications

## Open Questions

(none — all 6 Metis open questions answered by this design; Oracle consultation confirmed 8 concrete decisions.)
