// SCOPE: TEST-FIXTURE
/**
 * test_kfd_e2e_bridge.cpp — KFD L1↔L2 bridge E2E test
 *
 * Verifies GpuDriverClient → UsrLinuxEmu GpgpuDevice → KFD sim bridge
 * end-to-end for 5 KFD ioctls:
 *   CREATE_QUEUE, GET_PROCESS_APERTURE, UPDATE_QUEUE,
 *   MAP_MEMORY, UNMAP_MEMORY
 *
 * Uses real GpuDriverClient (needs /dev/gpgpu0 + plugin loaded).
 * Gracefully SKIPs when /dev/gpgpu0 not available.
 *
 * Counterpart to UsrLinuxEmu/tests/test_kfd_l1_l2_bridge_standalone.cpp (Phase A).
 * C-12 E.2.4 L1↔L2 bridge cross-repo sync.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "test_fixture/gpu_driver_client.h"

#include <sys/stat.h>
#include <memory>

using async_task::gpu::GpuDriverClient;

namespace {

class RealGpuFixture {
public:
  RealGpuFixture() {
    struct stat st;
    if (::stat("/dev/gpgpu0", &st) != 0) {
      available_ = false;
      return;
    }
    client_ = std::make_unique<GpuDriverClient>("/dev/gpgpu0");
    if (client_->open() != 0) {
      client_.reset();
      available_ = false;
      return;
    }
    available_ = true;
  }

  bool is_available() const { return available_; }

  uint64_t alloc_bo(uint64_t size, uint32_t domain, uint32_t flags) {
    return client_->alloc_bo(size, domain | flags);
  }

  int free_bo(uint64_t bo_handle) {
    return client_->free_bo(bo_handle);
  }

  uint64_t create_va_space() {
    return client_->create_va_space(0);
  }

  int destroy_va_space(uint64_t handle) {
    return client_->destroy_va_space(handle);
  }

  uint64_t create_queue(uint64_t va_space, uint32_t type, uint32_t size) {
    return client_->create_queue(va_space, type, size, 0);
  }

  int destroy_queue(uint64_t q_handle) {
    return client_->destroy_queue(q_handle);
  }

  long kfd_map_memory(uint32_t handle, uint64_t* out_gpu_va) {
    return client_->kfd_map_memory(handle, 4096, out_gpu_va);
  }

  long kfd_unmap_memory(uint32_t handle) {
    return client_->kfd_unmap_memory(handle);
  }

  long kfd_get_process_aperture(uint32_t num_nodes,
      struct gpu_aperture_info* out_apertures) {
    return client_->kfd_get_process_aperture(num_nodes, out_apertures);
  }

  long kfd_update_queue(uint64_t handle, uint32_t flags) {
    return client_->kfd_update_queue(handle, flags);
  }

  std::unique_ptr<GpuDriverClient> client_;
  bool available_{false};
};

}  // namespace

// ═══════════════════════════════════════════════════════════════
// Test 1: MAP_MEMORY / UNMAP_MEMORY E2E
// ═══════════════════════════════════════════════════════════════

TEST_CASE_FIXTURE(RealGpuFixture,
    "KFD L1L2 bridge MAP_MEMORY / UNMAP_MEMORY E2E") {
  if (!is_available()) {
    MESSAGE("/dev/gpgpu0 not present — skipping KFD E2E test");
    return;
  }

  // Alloc BO for valid handle
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

  // Double-unmap is idempotent
  ret = kfd_unmap_memory(static_cast<uint32_t>(bo_handle));
  REQUIRE(ret == 0);

  // Cleanup
  REQUIRE(free_bo(bo_handle) == 0);
}

// ═══════════════════════════════════════════════════════════════
// Test 2: GET_PROCESS_APERTURE + UPDATE_QUEUE E2E
// ═══════════════════════════════════════════════════════════════

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

  // GET_PROCESS_APERTURE: multi-node
  struct gpu_aperture_info ap_multi[8] = {};
  ret = kfd_get_process_aperture(4, ap_multi);
  REQUIRE(ret == 0);
  REQUIRE(ap_multi[3].gpu_id == 3);

  // UPDATE_QUEUE: valid handle
  ret = kfd_update_queue(q_handle, 0);
  REQUIRE(ret == 0);

  // UPDATE_QUEUE: invalid handle → error
  ret = kfd_update_queue(0, 0);
  REQUIRE(ret < 0);

  // Cleanup
  REQUIRE(destroy_queue(q_handle) == 0);
  REQUIRE(destroy_va_space(va_handle) == 0);
}

// ═══════════════════════════════════════════════════════════════
// Test 3: Full KFD 5-ioctl chain E2E
// ═══════════════════════════════════════════════════════════════

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
