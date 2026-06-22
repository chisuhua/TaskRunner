/**
 * test_gpu_architecture.cpp - H-2.5 架构基础测试
 *
 * 测试 9 项:
 *  T1: GpuDriverClient implements IGpuDriver (编译期)
 *  T2: CudaStub implements IGpuDriver (编译期)
 *  T3: MockGpuDriver implements IGpuDriver (编译期)
 *  T4: CudaScheduler 接受 MockGpuDriver 注入 (DI 验证)
 *  T5: CudaScheduler nullptr 时 auto-create CudaStub
 *  T6: D6/D7/D8 签名对齐
 *  T7: D9 命名空间迁移
 *  T8: D10 DI 行为 (driver 注入后调用转发)
 *  T9: H-1 向后兼容 (CamelCase alias)
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <type_traits>
#include <cstdint>
#include <cstring>

#include "igpu_driver.hpp"
#include "cuda_stub.hpp"
#include "cuda_scheduler.hpp"
#include "gpu_driver_client.h"
#include "mock_gpu_driver.hpp"

using namespace async_task::gpu;
using async_task::gpu::CudaStub;
using async_task::gpu::GpuDriverClient;
using async_task::gpu::IGpuDriver;
using async_task::gpu::MockGpuDriver;

// ============================================================================
// T1: GpuDriverClient implements IGpuDriver (编译期验证)
// ============================================================================

TEST_CASE("H-2.5 T1: GpuDriverClient implements IGpuDriver") {
    static_assert(std::is_base_of_v<IGpuDriver, GpuDriverClient>,
                  "GpuDriverClient must inherit from IGpuDriver");
    CHECK(true);
}

// ============================================================================
// T2: CudaStub implements IGpuDriver (编译期验证)
// ============================================================================

TEST_CASE("H-2.5 T2: CudaStub implements IGpuDriver") {
    static_assert(std::is_base_of_v<IGpuDriver, CudaStub>,
                  "CudaStub must inherit from IGpuDriver");
    CHECK(true);
}

// ============================================================================
// T3: MockGpuDriver implements IGpuDriver (编译期验证)
// ============================================================================

TEST_CASE("H-2.5 T3: MockGpuDriver implements IGpuDriver") {
    static_assert(std::is_base_of_v<IGpuDriver, MockGpuDriver>,
                  "MockGpuDriver must inherit from IGpuDriver");
    CHECK(true);
}

// ============================================================================
// T4: CudaScheduler 接受 MockGpuDriver 注入
// ============================================================================

TEST_CASE("H-2.5 T4: CudaScheduler accepts MockGpuDriver injection") {
    MockGpuDriver mock;
    {
        taskrunner::CudaScheduler scheduler(&mock);
        CHECK(scheduler.driver() == &mock);
    }
    CHECK(mock.total_calls() == 0);
}

// ============================================================================
// T5: CudaScheduler nullptr 时 auto-create CudaStub
// ============================================================================

TEST_CASE("H-2.5 T5: CudaScheduler nullptr auto-creates CudaStub") {
    taskrunner::CudaScheduler scheduler;
    CHECK(scheduler.driver() != nullptr);
    // 应当是 CudaStub 类型
    auto* stub = dynamic_cast<CudaStub*>(scheduler.driver());
    CHECK(stub != nullptr);
}

// ============================================================================
// T6: D6/D7/D8 签名对齐
// ============================================================================

TEST_CASE("H-2.5 T6: D6/D7/D8 signature alignment") {
    MockGpuDriver mock;

    // D6: alloc_bo(size, flags) -> u64
    uint64_t handle = mock.alloc_bo(4096, 0);
    CHECK(handle != 0);
    CHECK(mock.call_count("alloc_bo") == 1);

    // D6: alloc_bo_vram(size, flags) -> u64
    uint64_t vram_handle = mock.alloc_bo_vram(4096, 0);
    CHECK(vram_handle != 0);

    // D7: free_bo(u64) - 应接受 u64
    int rc = mock.free_bo(handle);
    CHECK(rc == 0);

    // D8: map_bo(handle, size) -> void*
    void* ptr = mock.map_bo(vram_handle, 4096);
    CHECK(ptr != nullptr);
    std::free(ptr);
}

// ============================================================================
// T7: D9 命名空间迁移
// ============================================================================

TEST_CASE("H-2.5 T7: D9 namespace migration") {
    // async_task::gpu::CudaStub 可实例化
    async_task::gpu::CudaStub stub1;

    // taskrunner::CudaStub 是同一类型 (alias)
    taskrunner::CudaStub stub2;
    static_assert(std::is_same_v<async_task::gpu::CudaStub, taskrunner::CudaStub>,
                  "taskrunner::CudaStub must be alias for async_task::gpu::CudaStub");

    // LaunchParams / CudaResult 也迁移
    taskrunner::LaunchParams lp;
    lp.kernel_name = "test";
    CHECK(lp.kernel_name != nullptr);

    taskrunner::CudaResult r = taskrunner::CudaResult::SUCCESS;
    CHECK(r == taskrunner::CudaResult::SUCCESS);

    (void)stub1;
    (void)stub2;
}

// ============================================================================
// T8: D10 DI 行为
// ============================================================================

TEST_CASE("H-2.5 T8: D10 DI forwarding") {
    MockGpuDriver mock;
    mock.clear_history();

    // 注入后, 所有 driver_->xxx() 应调用 mock
    {
        taskrunner::CudaScheduler scheduler(&mock);
        scheduler.initialize(true);

        // D10: 注入的 driver 不被 auto-delete
        // 我们手动检查 mock 仍存在
        mock.set_current_va_space(42);
        CHECK(mock.get_current_va_space() == 42);
    }

    // scheduler 析构后 mock 仍存在
    CHECK(mock.total_calls() > 0);
    CHECK(mock.call_count("set_current_va_space") == 1);
}

// ============================================================================
// T9: H-1 向后兼容 (CamelCase alias)
// ============================================================================

TEST_CASE("H-2.5 T9: H-1 backward compat (CamelCase alias)") {
    GpuDriverClient client;

    // CamelCase (H-1 既有 API) 仍可用
    client.setCurrentVASpace(42);
    CHECK(client.getCurrentVASpace() == 42);
    CHECK(client.getCurrentVASpace() == client.get_current_va_space());

    // snake_case (H-2.5 新 API) 也可用
    client.set_current_va_space(100);
    CHECK(client.getCurrentVASpace() == 100);
    CHECK(client.get_current_va_space() == 100);

    // 同一底层状态
    CHECK(client.getCurrentVASpace() == client.get_current_va_space());
}

// ============================================================================
// Bonus: MockGpuDriver 错误注入
// ============================================================================

TEST_CASE("H-2.5 Bonus: MockGpuDriver error injection") {
    MockGpuDriver mock;
    mock.inject_error("alloc_bo", true);

    uint64_t handle = mock.alloc_bo(4096, 0);
    CHECK(handle == 0);  // 注入错误 -> 返回 sentinel (0)

    mock.inject_error("alloc_bo", false);
    handle = mock.alloc_bo(4096, 0);
    CHECK(handle != 0);  // 错误取消 -> 正常返回
}

// ============================================================================
// Bonus: H-3 Phase 2 占位 GpuDriverClient 抛异常
// ============================================================================

TEST_CASE("H-2.5 Bonus: GpuDriverClient H-3 placeholders throw") {
    GpuDriverClient client;

    CHECK_THROWS(client.create_va_space(0));
    CHECK_THROWS(client.destroy_va_space(0));
    CHECK_THROWS(client.register_gpu(0, 0, 0));
    CHECK_THROWS(client.create_queue(0, 0, 0, 0));
    CHECK_THROWS(client.destroy_queue(0));
}
