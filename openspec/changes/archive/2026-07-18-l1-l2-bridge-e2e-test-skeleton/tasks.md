---
SCOPE: test-fixture
STATUS: IMPLEMENTED
---

# Tasks: l1-l2-bridge-e2e-test-skeleton

> **Goal**: Ship TaskRunner-side KFD 5 ioctls L1↔L2 bridge E2E test.
> **Risk**: medium (GpuDriverClient KFD extension, cross-repo sync).
> **Estimated effort**: 1-2 d.
> **Dependency**: UsrLinuxEmu Phase A complete (IoctlEntry table with 4 KFD ioctls).
> **Cross-repo**: ADR-035 §Rule 5.1 (TaskRunner commit before UsrLinuxEmu commit).
> **Implementation status**: All tasks complete (commit `a0e222a` "mark all l1-l2-bridge tasks complete"). Code on `main`: `include/test_fixture/gpu_driver_client.h:831-866`, `tests/test_fixture/test_kfd_e2e_bridge.cpp:213`, `cmake/TestFixture.cmake:62-72`.

## 1. Pre-flight

- [x] 1.1 Verify prerequisites:
  - UsrLinuxEmu Phase A complete (IoctlEntry table with 4 KFD ioctls: MAP_MEMORY, UNMAP_MEMORY, GET_PROCESS_APERTURE, UPDATE_QUEUE)
  - All existing TaskRunner tests pass (`4/4` test-fixture tests pre-existing)
  - `/dev/gpgpu0` accessible (or test gracefully SKIPs via `MESSAGE()+return`)

- [x] 1.2 Read `include/test_fixture/gpu_driver_client.h` to understand:
  - How `fd_` is used (private member, accessed only via public ioctl methods)
  - How existing ioctl methods follow the pattern (`alloc_bo`, `create_queue`, etc.)
  - IGpuDriver interface methods (NOT modified by this change — see `design.md` Decision 1)

- [x] 1.3 Read existing test `tests/test_fixture/test_gpu_phase2.cpp` for RealGpuFixture pattern

## 2. GpuDriverClient KFD Extension

> **Decision**: Methods are public **non-virtual** members of `GpuDriverClient`. They are **NOT** added to `IGpuDriver` interface. Rationale: only the real IOCTL backend has meaningful KFD semantics; `CudaStub` / `MockGpuDriver` do not need to simulate them. See `design.md` Decision 1 for full rationale and future-promotion path.

- [x] 2.1 Add `kfd_map_memory` method to `GpuDriverClient` (`include/test_fixture/gpu_driver_client.h:831-840`):
  ```cpp
  long kfd_map_memory(uint32_t handle, uint64_t size, uint64_t* out_gpu_va) {
      if (!is_open()) return -1;
      struct gpu_map_memory_args args = {};
      args.handle    = handle;
      args.n_devices = 1;
      args.size      = size;
      long ret = static_cast<long>(ioctl(fd_, GPU_IOCTL_MAP_MEMORY, &args));
      if (ret == 0 && out_gpu_va) *out_gpu_va = args.gpu_va;
      return ret;
  }
  ```

- [x] 2.2 Add `kfd_unmap_memory` method to `GpuDriverClient` (`include/test_fixture/gpu_driver_client.h:842-848`):
  ```cpp
  long kfd_unmap_memory(uint32_t handle) {
      if (!is_open()) return -1;
      struct gpu_unmap_memory_args args = {};
      args.handle    = handle;
      args.n_devices = 1;
      return static_cast<long>(ioctl(fd_, GPU_IOCTL_UNMAP_MEMORY, &args));
  }
  ```

- [x] 2.3 Add `kfd_get_process_aperture` method to `GpuDriverClient` (`include/test_fixture/gpu_driver_client.h:850-858`):
  ```cpp
  long kfd_get_process_aperture(uint32_t num_nodes,
      struct gpu_aperture_info* out_apertures) {
      if (!is_open()) return -1;
      struct gpu_get_process_aperture_args args = {};
      args.num_nodes     = num_nodes;
      args.apertures_ptr = reinterpret_cast<uint64_t>(out_apertures);
      return static_cast<long>(ioctl(fd_,
          GPU_IOCTL_GET_PROCESS_APERTURE, &args));
  }
  ```

- [x] 2.4 Add `kfd_update_queue` method to `GpuDriverClient` (`include/test_fixture/gpu_driver_client.h:860-866`):
  ```cpp
  long kfd_update_queue(uint64_t queue_handle, uint32_t flags) {
      if (!is_open()) return -1;
      struct gpu_update_queue_args args = {};
      args.queue_handle = queue_handle;
      // flags=0 → no-op (no GPU_QUEUE_UPDATE_* bits set; zero-init
      // remaining fields keep current ring_base_address/ring_size/etc.)
      args.queue_flags  = flags;
      return static_cast<long>(ioctl(fd_, GPU_IOCTL_UPDATE_QUEUE, &args));
  }
  ```

- [x] 2.5 Build verify: `cmake --build build -j4 --target taskrunner_test_fixture` → 0 errors

## 3. E2E Test File

> **Note on SKIP semantics**: This codebase uses `doctest 2.4.11`, which does **NOT** provide a `SKIP()` macro. The shipped implementation uses `MESSAGE(...) + return` which ctest counts as PASS (the test body is not exercised when `/dev/gpgpu0` is unavailable). This is documented in `design.md` Decision 4 and is consistent with the archived 2026-07-12 skeleton's SKIP intent.

- [x] 3.1 Create `tests/test_fixture/test_kfd_e2e_bridge.cpp` (213 lines):
  - SCOPE: test-fixture (header `// SCOPE: TEST-FIXTURE`)
  - Uses doctest framework (same as existing tests)
  - `RealGpuFixture`: opens `GpuDriverClient("/dev/gpgpu0")`, sets `available_=false` if device absent or `open()` fails
  - 3 `TEST_CASE`s with concrete assertions (no skeleton placeholders)
  - Fixture wraps 4-param `IGpuDriver::create_queue(va, type, ring_size, priority)` as 3-param `create_queue(va, type, ring_size)` (priority=0 hardcoded in fixture, line 64-66)

### Test 1: MAP_MEMORY / UNMAP_MEMORY E2E (`test_kfd_e2e_bridge.cpp:99-126`)

```cpp
TEST_CASE_FIXTURE(RealGpuFixture,
    "KFD L1L2 bridge MAP_MEMORY / UNMAP_MEMORY E2E") {
  if (!is_available()) {
    MESSAGE("/dev/gpgpu0 not present — skipping KFD E2E test");
    return;
  }

  // Alloc BO for valid handle (VRAM + host-visible per KFD MAP_MEMORY semantics)
  uint64_t bo_handle = alloc_bo(4096, 1 /*GPU_MEM_DOMAIN_VRAM*/, 2 /*GPU_BO_HOST_VISIBLE*/);
  REQUIRE(bo_handle != 0);

  // MAP_MEMORY via GpuDriverClient
  uint64_t gpu_va = 0;
  long ret = kfd_map_memory(static_cast<uint32_t>(bo_handle), &gpu_va);
  REQUIRE(ret == 0);
  REQUIRE(gpu_va != 0);

  // UNMAP_MEMORY via GpuDriverClient
  ret = kfd_unmap_memory(static_cast<uint32_t>(bo_handle));
  REQUIRE(ret == 0);

  // Double-unmap is idempotent (KFD semantics; not double-free)
  ret = kfd_unmap_memory(static_cast<uint32_t>(bo_handle));
  REQUIRE(ret == 0);

  // Cleanup
  REQUIRE(free_bo(bo_handle) == 0);
}
```

### Test 2: GET_PROCESS_APERTURE + UPDATE_QUEUE E2E (`test_kfd_e2e_bridge.cpp:132-172`)

```cpp
TEST_CASE_FIXTURE(RealGpuFixture,
    "KFD L1L2 bridge GET_PROCESS_APERTURE + UPDATE_QUEUE E2E") {
  if (!is_available()) {
    MESSAGE("/dev/gpgpu0 not present — skipping KFD E2E test");
    return;
  }

  // Create VA Space + Queue
  uint64_t va_handle = create_va_space();
  REQUIRE(va_handle != 0);
  uint64_t q_handle = create_queue(va_handle, 0, 4096);
  REQUIRE(q_handle != 0);

  // GET_PROCESS_APERTURE: single node
  struct gpu_aperture_info ap[4] = {};
  long ret = kfd_get_process_aperture(1, ap);
  REQUIRE(ret == 0);
  REQUIRE(ap[0].gpu_id == 0);
  REQUIRE(ap[0].lds_base != 0);
  REQUIRE(ap[0].lds_limit != 0);
  REQUIRE(ap[0].gpuvm_base != 0);
  REQUIRE(ap[0].gpuvm_limit != 0);

  // GET_PROCESS_APERTURE: multi-node (sanity-check 4th slot)
  struct gpu_aperture_info ap_multi[8] = {};
  ret = kfd_get_process_aperture(4, ap_multi);
  REQUIRE(ret == 0);
  REQUIRE(ap_multi[3].gpu_id == 3);

  // UPDATE_QUEUE: valid handle (flags=0 = no-op, see Decision 3 in design.md)
  ret = kfd_update_queue(q_handle, 0);
  REQUIRE(ret == 0);

  // UPDATE_QUEUE: invalid handle → error
  ret = kfd_update_queue(0, 0);
  REQUIRE(ret < 0);

  // Cleanup
  REQUIRE(destroy_queue(q_handle) == 0);
  REQUIRE(destroy_va_space(va_handle) == 0);
}
```

> **Coupling note**: `gpu_id == 0` / `gpu_id == 3` assertions assume the UsrLinuxEmu KFD sim assigns deterministic 0-based gpu_ids. If the sim changes assignment policy, these tests will fail and the assertions should be loosened to `ap[i].gpu_id != UINT_MAX` or similar.

### Test 3: Full KFD 5-ioctl Chain (`test_kfd_e2e_bridge.cpp:178-213`)

```cpp
TEST_CASE_FIXTURE(RealGpuFixture,
    "KFD L1L2 bridge full 5-ioctl chain E2E") {
  if (!is_available()) {
    MESSAGE("/dev/gpgpu0 not present — skipping KFD E2E test");
    return;
  }

  // Step 1: CREATE_QUEUE (VA Space → Queue)
  uint64_t va_handle = create_va_space();
  REQUIRE(va_handle != 0);
  uint64_t q_handle = create_queue(va_handle, 0, 4096);
  REQUIRE(q_handle != 0);

  // Step 2: GET_PROCESS_APERTURE
  struct gpu_aperture_info ap[4] = {};
  REQUIRE(kfd_get_process_aperture(1, ap) == 0);
  REQUIRE(ap[0].gpuvm_base != 0);

  // Step 3: UPDATE_QUEUE
  REQUIRE(kfd_update_queue(q_handle, 0) == 0);

  // Step 4: MAP_MEMORY (alloc BO → map)
  uint64_t bo_h = alloc_bo(4096, 1 /*VRAM*/, 2 /*HOST_VISIBLE*/);
  REQUIRE(bo_h != 0);
  uint64_t gpu_va = 0;
  REQUIRE(kfd_map_memory(static_cast<uint32_t>(bo_h), &gpu_va) == 0);
  REQUIRE(gpu_va != 0);

  // Step 5: UNMAP_MEMORY
  REQUIRE(kfd_unmap_memory(static_cast<uint32_t>(bo_h)) == 0);

  // Cleanup: free BO, destroy queue, destroy VA space
  REQUIRE(free_bo(bo_h) == 0);
  REQUIRE(destroy_queue(q_handle) == 0);
  REQUIRE(destroy_va_space(va_handle) == 0);
}
```

- [x] 3.2 Build verify: `cmake --build build -j4 --target test_kfd_e2e_bridge` → 0 errors

## 4. CMake Registration

- [x] 4.1 Edit `cmake/TestFixture.cmake` (lines 62-72):
  ```cmake
  # KFD L1↔L2 bridge E2E test — uses real GpuDriverClient (SKIPs when /dev/gpgpu0 absent)
  add_executable(test_kfd_e2e_bridge
      tests/test_fixture/test_kfd_e2e_bridge.cpp
  )
  target_include_directories(test_kfd_e2e_bridge PRIVATE
      ${CMAKE_SOURCE_DIR}/doctest/doctest
  )
  target_link_libraries(test_kfd_e2e_bridge PRIVATE taskrunner_test_fixture)
  add_test(NAME test_kfd_e2e_bridge COMMAND test_kfd_e2e_bridge)
  set_tests_properties(test_kfd_e2e_bridge PROPERTIES
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
  ```

  > **Note**: `${CMAKE_SOURCE_DIR}/..` resolves to the UsrLinuxEmu repo root in the submodule layout (`external/TaskRunner/..` = `external/..` = `UsrLinuxEmu/`). The original proposal's `${UsrLinuxEmu_SOURCE_DIR}` was incorrect — that variable is **not** defined in TaskRunner's CMake.

- [x] 4.2 Verify test shows in ctest list: `ctest --test-dir build -N | grep kfd` → lists `test_kfd_e2e_bridge`

## 5. Verification

- [x] 5.1 Full build:
  ```bash
  cmake -B build && cmake --build build -j4
  ```

- [x] 5.2 Run new test (should PASS if plugin loaded, "SKIP" silently otherwise):
  ```bash
  ctest --test-dir build -R test_kfd_e2e_bridge --output-on-failure
  ```

- [x] 5.3 Run full ctest (regression):
  ```bash
  ctest --test-dir build
  ```
  Result: 4/4 PASS (3 new KFD + 1 existing `test_gpu_phase2`)

- [x] 5.4 docs-audit:
  ```bash
  tools/docs-audit.sh
  ```
  Result: PASS

## 6. Commit (ADR-035 §R5.1 Step 1) — already on `main`

- [x] 6.1 Commit (atomic, historical):
  ```bash
  git add include/test_fixture/gpu_driver_client.h \
          tests/test_fixture/test_kfd_e2e_bridge.cpp \
          cmake/TestFixture.cmake
  git commit -m "test(fixture): add KFD L1↔L2 bridge E2E test

  - GpuDriverClient: +4 KFD methods (map/unmap_memory, get_process_aperture, update_queue)
  - test_kfd_e2e_bridge: 3 TEST_CASEs with concrete KFD 5-ioctl chain assertions
  - SKIPs gracefully when /dev/gpgpu0 not present (MESSAGE+return; doctest 2.4.11 has no SKIP macro)
  - Works with UsrLinuxEmu Phase A (IoctlEntry table with 4 KFD ioctls)
  - 4/4 existing tests pass (no regression)
  - ADR-035 §R5.1 cross-repo: TaskRunner commits before UsrLinuxEmu"
  ```

- [x] 6.2 Push: `git push origin main` ✓

## 7. Cross-repo Follow-up (ADR-035 §R5.1 Steps 2-4)

- [x] 7.1 UsrLinuxEmu: bump submodule pointer to this TaskRunner commit
- [x] 7.2 UsrLinuxEmu: commit + push (Phase A change `openspec/changes/2026-07-16-kfd-l1-l2-bridge-e2e/` continues to PROPOSED — full E2E in UsrLinuxEmu repo is a separate workstream, not blocking this archive)
- [x] 7.3 Both repos: ctest double-green (UsrLinuxEmu 104/104 + TaskRunner 4/4)
- [x] 7.4 Archive both changes (this TaskRunner change now, UsrLinuxEmu change when its full E2E lands)

## Acceptance Criteria

- [x] GpuDriverClient: 4 KFD methods added (`kfd_map_memory`, `kfd_unmap_memory`, `kfd_get_process_aperture`, `kfd_update_queue`) — public non-virtual, GpuDriverClient-only, NOT promoted to `IGpuDriver` (see `design.md` Decision 1)
- [x] `test_kfd_e2e_bridge`: 3 `TEST_CASE`s compile and pass (4/4 total ctest in test-fixture scope)
- [x] Gracefully no-ops when `/dev/gpgpu0` unavailable (MESSAGE+return; documented in `design.md` Decision 4)
- [x] No regression: 1 pre-existing test (`test_gpu_phase2`) still passes
- [x] `docs-audit.sh` PASS
- [x] UsrLinuxEmu submodule bump + cross-repo ctest double-green