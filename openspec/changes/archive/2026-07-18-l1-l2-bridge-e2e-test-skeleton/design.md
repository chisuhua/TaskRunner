---
SCOPE: test-fixture
STATUS: IMPLEMENTED
---

## Context

After `phase3-1-igpu-driver-extension` (H-3, 2026-06-23) and `2026-07-16-kfd-l1-l2-bridge-e2e` (UsrLinuxEmu Phase A, 2026-07-12), the test coverage matrix for the L1↔L2 bridge looks like:

| Layer | Test | Scope | Status |
|---|---|---|---|
| L1 (UMD shim with MockGpuDriver) | `tests/umd/test_cu_graph.cpp` | umd-evolution | ✅ 32/32 |
| L2 (raw IOCTL with real plugin) | `UsrLinuxEmu/tests/test_kfd_l1_l2_bridge_standalone.cpp` | UsrLinuxEmu | ✅ 104/104 ctest |
| L1↔L2 (TaskRunner with real GpuDriverClient) | `tests/test_fixture/test_kfd_e2e_bridge.cpp` | test-fixture | ✅ 3/3 (this change) |
| L1↔L2 (UMD shim with real GpuDriverClient, CUDA Graph) | `tests/umd/test_cu_graph_e2e_standalone.cpp` | umd-evolution | ✅ SKIP (archived 2026-07-12 skeleton) |

This change ships the **test-fixture scope** row of the L1↔L2 bridge matrix. It complements the archived `2026-07-12-l1-l2-bridge-e2e-test-skeleton` (umd-evolution: cuGraphLaunch) — together they cover both KFD ioctl E2E and CUDA Graph E2E for the L1↔L2 bridge.

**C-12 E.2.4 origin**: The KFD multi-file integration left a deferred test on the TaskRunner side. UsrLinuxEmu Phase A added the IoctlEntry table entries and 3 L2-side tests; TaskRunner side was missing the symmetric test. This change closes that gap.

## Goals / Non-Goals

**Goals:**
- 4 KFD public non-virtual methods on `GpuDriverClient` (`kfd_map_memory`, `kfd_unmap_memory`, `kfd_get_process_aperture`, `kfd_update_queue`)
- 3 `TEST_CASE`s in `tests/test_fixture/test_kfd_e2e_bridge.cpp`:
  - MAP_MEMORY / UNMAP_MEMORY E2E (with double-unmap idempotency check)
  - GET_PROCESS_APERTURE + UPDATE_QUEUE E2E (single/multi-node + invalid handle error)
  - Full 5-ioctl chain E2E
- `RealGpuFixture` with `/dev/gpgpu0` detection and graceful skip
- CMake target registered and discoverable via `ctest -N`
- Cross-repo sync per ADR-035 §R5.1

**Non-Goals:**
- Modifying `IGpuDriver` interface (KFD methods remain GpuDriverClient-only)
- Replacing UsrLinuxEmu-side L2 tests
- Replacing L1 (mock) test_cu_graph 32/32 tests
- Building real device file infrastructure (UsrLinuxEmu VFS work)
- Implementing the full real test in UsrLinuxEmu (separate UsrLinuxEmu-side change)

## Decisions

### Decision 1: KFD methods are public non-virtual members of `GpuDriverClient` (NOT promoted to `IGpuDriver`)

The 4 KFD methods are added as **public non-virtual** methods on `GpuDriverClient` (`include/test_fixture/gpu_driver_client.h:831-866`). They are NOT added to the `IGpuDriver` abstract interface (`include/shared/igpu_driver.hpp`).

**Rationale:**
- Only the real IOCTL backend (`GpuDriverClient`) has meaningful KFD semantics — KFD ioctls require a real `/dev/gpgpu0` fd and the UsrLinuxEmu plugin's KFD sim.
- `CudaStub` (in-process implementation in `src/test_fixture/cuda_stub.cpp`) and `MockGpuDriver` (testing mock) do not need to simulate KFD ioctls.
- Adding KFD as virtual methods on `IGpuDriver` would force `CudaStub` and `MockGpuDriver` to provide meaningless stub implementations that return -1.

**Future-promotion path:** If a future change requires UMD shim to call KFD ioctls through an `IGpuDriver*` pointer, a separate change should:
1. Add virtual `kfd_*` methods to `IGpuDriver`
2. Provide `CudaStub` implementations (return `-ENOSYS` or similar)
3. Provide `MockGpuDriver` implementations (return configurable values)
4. Migrate `GpuDriverClient` methods from concrete to `override`

**Trade-off:** The current design creates an asymmetry — `IGpuDriver*` callers cannot invoke KFD methods, only `GpuDriverClient*` callers can. This is acceptable because no current caller needs KFD through `IGpuDriver*`.

### Decision 2: `fd_` remains private; risk of exposure is mitigated

The 4 KFD methods are public members but **encapsulate** the `ioctl(fd_, ...)` call. The private `fd_` member is not exposed via raw ioctl accessor or getter. This matches the existing pattern used by `alloc_bo`, `create_queue`, etc.

**Original proposal risk acknowledgment:** The proposal §Risk section worried about "fd 暴露" (fd exposure). The actual implementation does not introduce this risk — `fd_` access is encapsulated behind public methods that perform the ioctl and return a typed result.

### Decision 3: `queue_flags=0` is a documented no-op

`gpu_update_queue_args` (gpu_ioctl.h:371-379) has 6 fields:
```c
struct gpu_update_queue_args {
  gpu_queue_handle_t queue_handle;   // u64
  u64 ring_base_address;
  u64 ring_size;
  u32 queue_percent;
  u32 queue_priority;
  u32 queue_flags;                   // GPU_QUEUE_UPDATE_* bitmask
  u32 pad;
};
```

The `kfd_update_queue` implementation only sets `queue_handle` and `queue_flags`. The other 4 fields are zero-initialized by `args = {}`. When `flags = 0`, no `GPU_QUEUE_UPDATE_*` bits are set, so the ioctl handler should interpret this as "no properties to update" and return success (the KFD sim does this).

**Implication:** The test assertion `REQUIRE(kfd_update_queue(q_handle, 0) == 0)` verifies that the ioctl **dispatch** works (struct layout, magic number, fd validity), but does not verify that any queue property was actually updated. This is acceptable for an L1↔L2 bridge smoke test — actual update-queue semantics belong in UsrLinuxEmu-side tests.

**Future hardening:** To meaningfully verify UPDATE_QUEUE, the test should:
1. Use `flags = GPU_QUEUE_UPDATE_PRIORITY` (or similar)
2. Pass a non-default `queue_priority` value
3. Verify the kernel-side state changed (requires a getter ioctl or shared-memory peek)

### Decision 4: SKIP semantics use `MESSAGE() + return` (not doctest `SKIP()`)

This codebase uses `doctest 2.4.11`, which does **NOT** provide a `SKIP()` macro. The shipped implementation uses:

```cpp
TEST_CASE_FIXTURE(RealGpuFixture, "...") {
  if (!is_available()) {
    MESSAGE("/dev/gpgpu0 not present — skipping KFD E2E test");
    return;
  }
  // ... real assertions
}
```

ctest counts this as **PASS** (the test exits 0 even when body is not exercised). This is consistent with the archived 2026-07-12 skeleton's stated intent ("gracefully SKIPs when /dev/gpgpu0 unavailable").

**Trade-off:** ctest output cannot distinguish "real PASS" from "skipped via no-op". CI metrics lose coverage granularity. We accept this trade-off because:
1. doctest 2.4.11 has no SKIP macro (upgrading doctest is a separate change)
2. The archived skeleton's SKIP behavior also relied on this mechanism
3. The 3/3 PASS output is sufficient signal that the test binary loaded successfully

**Future improvement:** When doctest is upgraded to 2.4+ (which has `DOCTEST_SKIP` macro), replace `MESSAGE() + return` with `DOCTEST_SKIP("/dev/gpgpu0 not present")`. This will make ctest report SKIP separately from PASS.

### Decision 5: `handle` u32 truncation is accepted (with documentation)

`gpu_map_memory_args.handle` and `gpu_unmap_memory_args.handle` are `u32` (gpu_ioctl.h:396, 415). `alloc_bo` returns `uint64_t` (`gpu_driver_client.h:229`). Tests use `static_cast<uint32_t>(bo_handle)` to truncate.

**Current safety:** `gpu_alloc_bo_args.handle` is also `u32` (gpu_ioctl.h:113), so BO handle values never exceed u32 range. Truncation is a no-op.

**Future risk:** If UsrLinuxEmu widens `gpu_alloc_bo_args.handle` to `u64` (consistent with the free_bo D7 widening path), all `static_cast<uint32_t>(bo_handle)` calls will silently truncate, producing incorrect mapping.

**Mitigation:** When KFD map/unmap structs are widened to `u64`, change KFD method signatures to accept `uint64_t handle`. The truncation point moves to the kernel boundary (which can validate against current handle table).

### Decision 6: `RealGpuFixture` wraps 4-param `IGpuDriver::create_queue` as 3-param helper

`IGpuDriver::create_queue` has 4 parameters: `(va_space_handle, queue_type, priority, ring_buffer_size)`. The fixture wraps this as `create_queue(va, type, ring_size)` with `priority=0` hardcoded:

```cpp
uint64_t create_queue(uint64_t va_space, uint32_t type, uint32_t size) {
  return client_->create_queue(va_space, type, size, 0);  // priority=0
}
```

**Rationale:** KFD queue priority semantics are out of scope for L1↔L2 bridge smoke testing. Defaulting to `priority=0` matches the UsrLinuxEmu KFD sim default.

**Future extension:** If priority variation needs testing, add a separate fixture method `create_queue_with_priority(va, type, size, priority)`.

### Decision 7: Test 2 asserts `gpu_id == 0` and `gpu_id == 3` (simulator-coupled)

The single-node and multi-node aperture tests assert specific gpu_id values:
- `REQUIRE(ap[0].gpu_id == 0)` (Test 2 line 149)
- `REQUIRE(ap_multi[3].gpu_id == 3)` (Test 2 line 159)

**Risk:** These assertions are tightly coupled to the UsrLinuxEmu KFD sim's gpu_id assignment policy. If the sim changes assignment (e.g., to UUID-based or PCI-BDF-based), these tests fail.

**Acceptance:** The coupling is acceptable because:
1. The KFD sim is in-house code; we control its assignment policy
2. The assertions document the **expected** assignment (deterministic 0-based)
3. If the sim policy changes, the test acts as a forcing function to update both sides together

## Risks / Trade-offs

| # | Risk | Mitigation | Status |
|---|------|------------|--------|
| 1 | `fd_` exposure via public KFD methods | Methods encapsulate ioctl; no raw fd accessor | ✅ Mitigated (Decision 2) |
| 2 | `handle` u32 truncation silently breaks on future widening | Document truncation; widen signatures when kernel widens | ⚠️ Accepted (Decision 5) |
| 3 | `queue_flags=0` no-op gives weak verification | Documented; future hardening via GPU_QUEUE_UPDATE_* | ⚠️ Accepted (Decision 3) |
| 4 | SKIP vs PASS ambiguity in ctest | Acceptable for doctest 2.4.11; upgrade path documented | ⚠️ Accepted (Decision 4) |
| 5 | Test 2 gpu_id assertions coupled to sim internals | Documented coupling; tests act as forcing function | ⚠️ Accepted (Decision 7) |
| 6 | IGpuDriver asymmetry (KFD not virtual) | Documented future-promotion path | ⚠️ Accepted (Decision 1) |
| 7 | Cross-repo sync ordering (TaskRunner before UsrLinuxEmu) | Followed ADR-035 §R5.1 4-step protocol | ✅ Mitigated |

## Verification

```bash
# Build the KFD E2E test
cmake -B build && cmake --build build -j4 --target test_kfd_e2e_bridge

# Run (PASS if /dev/gpgpu0 + plugin loaded; silent PASS otherwise)
ctest --test-dir build -R test_kfd_e2e_bridge --output-on-failure

# All test-fixture tests pass (no regression)
ctest --test-dir build -L test-fixture  # or -R test_kfd_e2e_bridge|test_gpu_phase2

# Expected: 4/4 PASS (3 KFD + 1 existing test_gpu_phase2)

# docs-audit
tools/docs-audit.sh  # expect: PASS
```

## Open Questions

- **Q**: Should `kfd_*` methods be promoted to `IGpuDriver` when UMD shim needs them?
  **A**: Defer to a future change. Current `GpuDriverClient*-only` design is intentional.

- **Q**: Should doctest be upgraded to use `DOCTEST_SKIP`?
  **A**: Separate change. Current `MESSAGE() + return` is acceptable.

- **Q**: Should the KFD sim's gpu_id assignment policy be documented externally?
  **A**: Yes — add to UsrLinuxEmu docs when Test 2 coupling becomes a maintenance burden.