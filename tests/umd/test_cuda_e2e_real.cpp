// SCOPE: UMD-EVOLUTION
// test_cuda_e2e_real.cpp — CUDA E2E real-path integration test.
//
// Verifies cuMemAlloc → cuMemcpyHtoD → cuLaunchKernel → cuStreamSynchronize
// → cuMemcpyDtoH full round-trip against real UsrLinuxEmu backend.
//
// Phase C+D required: fence async semantics + Puller MEMCPY + kernel no-op.
// Gracefully SKIPs when /dev/gpgpu0 not available.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <memory>

#include "test_fixture/gpu_driver_client.h"
#include "test_fixture/cuda_scheduler.hpp"
#include "shared/igpu_driver.hpp"
#include "umd/cuda_runtime_api.hpp"

namespace {

class RealGpuFixture {
 public:
  RealGpuFixture() {
    if (!std::filesystem::exists("/dev/gpgpu0")) {
      available_ = false;
      return;
    }
    client_ = std::make_unique<async_task::gpu::GpuDriverClient>();
    if (!client_ || client_->open() != 0) {
      client_.reset();
      available_ = false;
      return;
    }
    scheduler_ = std::make_unique<taskrunner::CudaScheduler>(client_.get());
    if (!scheduler_->initialize(true)) {
      scheduler_.reset();
      client_->close();
      client_.reset();
      available_ = false;
      return;
    }
    api_ = std::make_unique<async_task::umd::CudaRuntimeApi>(scheduler_.get());
    available_ = true;
  }
  ~RealGpuFixture() {
    api_.reset();
    if (scheduler_) {
      scheduler_->shutdown();
    }
    if (client_) {
      client_->close();
    }
  }

  bool is_available() const { return available_; }
  async_task::umd::CudaRuntimeApi* api() { return api_.get(); }
  taskrunner::CudaScheduler* scheduler() { return scheduler_.get(); }

 protected:
  std::unique_ptr<async_task::gpu::GpuDriverClient> client_;
  std::unique_ptr<taskrunner::CudaScheduler> scheduler_;
  std::unique_ptr<async_task::umd::CudaRuntimeApi> api_;
  bool available_{false};
};

}  // namespace

// ============================================================
// E.1 Happy Path
// ============================================================

TEST_CASE_FIXTURE(RealGpuFixture,
                  "CUDA E2E: alloc → memcpy H2D → launch → sync → memcpy D2H") {
  if (!is_available()) {
    MESSAGE("/dev/gpgpu0 not present — skipping E2E test");
    return;
  }
  REQUIRE(api() != nullptr);

  // 1. Alloc 4KB
  void* dptr = nullptr;
  auto err = api()->malloc(&dptr, 4096);
  REQUIRE_MESSAGE(err == async_task::umd::CudaError::Success,
                  "cuMemAlloc(4KB) should succeed");
  REQUIRE(dptr != nullptr);

  // 2. H2D memcpy
  const char input[] = "hello";
  err = api()->memcpy(dptr, input, sizeof(input),
                       async_task::umd::CudaMemcpyKind::HostToDevice);
  REQUIRE_MESSAGE(err == async_task::umd::CudaError::Success,
                  "cuMemcpyHtoD should succeed");

  // 3. Launch kernel (no-op through translator)
  void* kernel_args[] = { &dptr };
  err = api()->launch_kernel("dummy",
                              async_task::umd::Dim3{1, 1, 1},
                              async_task::umd::Dim3{1, 1, 1},
                              kernel_args, 0);
  REQUIRE_MESSAGE(err == async_task::umd::CudaError::Success,
                  "cuLaunchKernel(dummy) should succeed");

  // 4. D2H memcpy + verify
  char output[6] = {};
  err = api()->memcpy(output, dptr, sizeof(input),
                       async_task::umd::CudaMemcpyKind::DeviceToHost);
  REQUIRE_MESSAGE(err == async_task::umd::CudaError::Success,
                  "cuMemcpyDtoH should succeed");
  CHECK(std::memcmp(output, input, sizeof(input)) == 0);

  // 5. Free
  auto free_ret = scheduler()->submit_mem_free(
      reinterpret_cast<uint64_t>(dptr));
  CHECK(free_ret == 0);
}

// ============================================================
// E.2 Failure Path Tests
// ============================================================

TEST_CASE_FIXTURE(RealGpuFixture, "MEMCPY zero size returns error") {
  if (!is_available()) {
    MESSAGE("/dev/gpgpu0 not present — skipping");
    return;
  }

  void* dptr = nullptr;
  REQUIRE(api()->malloc(&dptr, 4096) == async_task::umd::CudaError::Success);

  // memcpy with 0 size → Puller HAL rejects in bounds check
  auto err = api()->memcpy(dptr, "x", 0,
                            async_task::umd::CudaMemcpyKind::HostToDevice);
  // CudaRuntimeApi::memcpy returns InvalidValue for size==0
  CHECK(err != async_task::umd::CudaError::Success);

  scheduler()->submit_mem_free(reinterpret_cast<uint64_t>(dptr));
}

TEST_CASE_FIXTURE(RealGpuFixture, "free then re-alloc works") {
  if (!is_available()) {
    MESSAGE("/dev/gpgpu0 not present — skipping");
    return;
  }

  void* dptr = nullptr;
  REQUIRE(api()->malloc(&dptr, 4096) == async_task::umd::CudaError::Success);
  REQUIRE(scheduler()->submit_mem_free(reinterpret_cast<uint64_t>(dptr)) == 0);

  // Re-alloc after free
  void* dptr2 = nullptr;
  REQUIRE(api()->malloc(&dptr2, 4096) == async_task::umd::CudaError::Success);
  REQUIRE(dptr2 != nullptr);
  scheduler()->submit_mem_free(reinterpret_cast<uint64_t>(dptr2));
}

TEST_CASE_FIXTURE(RealGpuFixture,
                  "memcpy to unallocated GPU VA returns error") {
  if (!is_available()) {
    MESSAGE("/dev/gpgpu0 not present — skipping");
    return;
  }

  // Use a garbage GPU VA (0xDEADBEEF) → HAL_HEAP_BASE range check fails
  void* bad_dptr = reinterpret_cast<void*>(static_cast<uintptr_t>(0xDEADBEEF));
  auto err = api()->memcpy(bad_dptr, "x", 1,
                            async_task::umd::CudaMemcpyKind::HostToDevice);
  // Should fail at HAL bounds check (dev_addr not in HAL_HEAP_BASE range)
  CHECK(err != async_task::umd::CudaError::Success);
}

TEST_CASE_FIXTURE(RealGpuFixture, "null pointer memcpy returns error") {
  if (!is_available()) {
    MESSAGE("/dev/gpgpu0 not present — skipping");
    return;
  }

  void* dptr = nullptr;
  REQUIRE(api()->malloc(&dptr, 4096) == async_task::umd::CudaError::Success);

  // nullptr host_ptr → CudaScheduler validate
  auto err = api()->memcpy(dptr, nullptr, 8,
                            async_task::umd::CudaMemcpyKind::HostToDevice);
  CHECK(err == async_task::umd::CudaError::InvalidValue);

  scheduler()->submit_mem_free(reinterpret_cast<uint64_t>(dptr));
}
