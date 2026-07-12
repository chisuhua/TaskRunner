// SCOPE: UMD-EVOLUTION
// test_cu_graph_e2e_standalone.cpp - L1↔L2 bridge E2E test skeleton.
//
// Status: SKELETON.
// Real test assertions (cuGraphCreate + cuGraphInstantiate + cuGraphLaunch
// via GpuDriverClient + cuStreamSynchronize waits on real fence) live in
// UsrLinuxEmu/tests/test_cu_graph_e2e_standalone.cpp.
//
// This binary:
//   1. Defines RealGpuFixture (setup GpuDriverClient + /dev/gpgpu0)
//   2. Runs 1 placeholder TEST_CASE that SKIPs if /dev/gpgpu0 is absent
//   3. Exits 0 on SKIP (doctest semantics)

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cuda.h>
#include <filesystem>
#include <memory>
#include <string>

#include "test_fixture/gpu_driver_client.h"
#include "test_fixture/cuda_stub.hpp"

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
    async_task::gpu::g_gpu_client = client_.get();
    available_ = true;
  }
  ~RealGpuFixture() {
    if (client_) {
      async_task::gpu::g_gpu_client = nullptr;
      client_->close();
    }
  }
  bool is_available() const { return available_; }

 protected:
  std::unique_ptr<async_task::gpu::GpuDriverClient> client_;
  bool available_{false};
};

}  // namespace

TEST_CASE_FIXTURE(RealGpuFixture,
                  "L1<->L2 bridge: cuGraphLaunch via real GpuDriverClient") {
  if (!is_available()) {
    // doctest 2.4.12 has no runtime SKIP macro; early-return makes the case
    // PASS (exit 0) — equivalent to "skipped" for CI gate purposes.
    MESSAGE("/dev/gpgpu0 not present; plugin not loaded. "
            "UsrLinuxEmu side fills in real assertions.");
    return;
  }
  // TODO(UsrLinuxEmu): replace this stub with the real E2E assertions:
  //   1. cuGraphCreate + cuGraphInstantiate
  //   2. cuGraphLaunch (real GpuDriverClient -> IOCTL -> Puller)
  //   3. cuStreamSynchronize (waits on real fence from Puller handleComplete)
  //   4. Verify: g_gpu_client->wait_fence was called with correct fence_id
  //   5. Verify: cuStreamSynchronize returned CUDA_SUCCESS
  MESSAGE("skeleton: real assertions are in UsrLinuxEmu side");
}
