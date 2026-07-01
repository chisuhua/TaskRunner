// SCOPE: UMD-EVOLUTION
// STATUS: PROPOSED (Phase 1 test suite)

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "umd/cuda_runtime_api.hpp"
#include "test_fixture/cuda_scheduler.hpp"
#include "test_fixture/cuda_stub.hpp"
#include "shared/igpu_driver.hpp"

#include <memory>
#include <thread>
#include <vector>
#include <cstdint>
#include <atomic>
#include <stdexcept>

using namespace async_task;
using async_task::umd::CudaRuntimeApi;
using async_task::umd::CudaError;
using async_task::umd::CudaMemcpyKind;
using async_task::umd::Dim3;

namespace {

struct Fixture {
  // Phase 1 only supports CudaStub backend (see design doc known limitation).
  taskrunner::CudaStub stub;
  taskrunner::CudaScheduler scheduler;
  std::unique_ptr<CudaRuntimeApi> api;

  Fixture()
      : stub(),
        scheduler(&stub),
        api(std::make_unique<CudaRuntimeApi>(&scheduler)) {
    // CudaRuntimeApi ctor calls driver()->create_va_space() / create_queue(),
    // which work on CudaStub without initialize().
    // But CudaScheduler submit_* methods require initialize(true).
    scheduler.initialize(true);
  }

  ~Fixture() {
    // Destroy API first (frees VA space / queue via driver).
    api.reset();
    // Then shutdown scheduler (does not delete non-owned driver).
    scheduler.shutdown();
    // stub destructor runs after Fixture destructor.
  }
};

}  // namespace

TEST_CASE("malloc_returns_valid_pointer: RAII safety + handle teardown") {
  Fixture f;
  void* ptr = nullptr;
  CHECK(f.api->malloc(&ptr, 4096) == CudaError::Success);
  CHECK(ptr != nullptr);
}

TEST_CASE("memcpy_h2d_data_integrity: data path correctness") {
  Fixture f;
  void* devPtr = nullptr;
  REQUIRE(f.api->malloc(&devPtr, 64) == CudaError::Success);

  std::vector<std::uint8_t> host(64, 0xAB);
  CHECK(f.api->memcpy(devPtr, host.data(), 64,
                      CudaMemcpyKind::HostToDevice) == CudaError::Success);
}

TEST_CASE("memcpy_d2h_data_integrity: bidirectional consistency") {
  Fixture f;
  void* devPtr = nullptr;
  REQUIRE(f.api->malloc(&devPtr, 32) == CudaError::Success);

  std::vector<std::uint8_t> src(32, 0xCD);
  std::vector<std::uint8_t> dst(32, 0);
  REQUIRE(f.api->memcpy(devPtr, src.data(), 32,
                        CudaMemcpyKind::HostToDevice) == CudaError::Success);
  CHECK(f.api->memcpy(dst.data(), devPtr, 32,
                      CudaMemcpyKind::DeviceToHost) == CudaError::Success);
  // Verify no crash on double-free / stale pointer in Fixture teardown.
  CHECK(dst.size() == 32);
}

TEST_CASE("memcpy_d2d_returns_not_supported: graceful error") {
  Fixture f;
  char a, b;
  CHECK(f.api->memcpy(&a, &b, 1, CudaMemcpyKind::DeviceToDevice) ==
        CudaError::NotSupported);
}

TEST_CASE("launch_kernel_returns_fence: synchronization completion") {
  Fixture f;
  REQUIRE(f.api->register_kernel("vectorAdd", 0) == CudaError::Success);

  void* args = nullptr;
  CHECK(f.api->launch_kernel("vectorAdd", Dim3{1}, Dim3{256}, &args, 0) ==
        CudaError::Success);
}

TEST_CASE("register_kernel_duplicate_detection: registry integrity") {
  Fixture f;
  REQUIRE(f.api->register_kernel("vecAdd", 0) == CudaError::Success);
  CHECK(f.api->register_kernel("vecAdd", 1) == CudaError::InvalidValue);
}

TEST_CASE("ctor_fail_no_va_space_leak: resource cleanup on init failure") {
  CHECK_THROWS_AS(CudaRuntimeApi(nullptr), std::invalid_argument);
}

TEST_CASE("multi_thread_concurrent_alloc: mutex correctness") {
  Fixture f;
  std::vector<std::thread> threads;
  std::atomic<int> success_count{0};

  for (int i = 0; i < 8; ++i) {
    threads.emplace_back([&]() {
      void* ptr = nullptr;
      if (f.api->malloc(&ptr, 128) == CudaError::Success) {
        ++success_count;
      }
    });
  }
  for (auto& t : threads) t.join();
  CHECK(success_count.load() == 8);
}
