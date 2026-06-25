// SCOPE: TEST-FIXTURE
/**
 * test_gpu_phase2.cpp - H-3 Phase 2 methods 集成测试 (12 cases)
 *
 * 测试 5 Phase 2 ioctl wrapper 方法 via IGpuDriver* DI 注入 (MockGpuDriver):
 *   T1-T5:    Success path (5)
 *   T6-T9:    Guard rejection verification (4) — MockGpuDriver guard 与 GpuDriverClient/CudaStub 一致 (H-3.5)
 *   T10:      R2 mapping contract (1) — stream_id = LOW32(queue_handle)
 *   T10b/c:   Bonus — R2 契约违反场景 (2)
 *
 * H-3.5 change: T6-T9 之前验证 mock 的实际行为 (mock 会被调用, 返回 canned value).
 * 现在改为验证 guard rejection (MockGpuDriver 5 个 Phase 2 方法添加 handle==0 / va_space==0
 * guards, 行为与 GpuDriverClient/CudaStub 一致). 真实 guard 测试由 GpuDriverClient/CudaStub
 * 实现保证 (已通过 H-3 spec L14-29, L52-57, L70-73, L93-96, L120-123).
 *
 * 使用 doctest 测试框架
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "shared/igpu_driver.hpp"
#include "test_fixture/cuda_scheduler.hpp"
#include "mock_gpu_driver.hpp"

using async_task::gpu::MockGpuDriver;
using taskrunner::CudaScheduler;

// ============================================================================
// T1: create_va_space returns non-zero handle
// ============================================================================

TEST_CASE("create_va_space_returns_nonzero_handle") {
    MockGpuDriver mock;
    mock.open();  // H-3.5: is_open guard requires open() first
    mock.set_canned_return("create_va_space", 1);
    CudaScheduler scheduler(&mock);

    uint64_t handle = scheduler.driver()->create_va_space(0);

    CHECK(handle == 1);
    CHECK(mock.call_count("create_va_space") == 1);
    CHECK(mock.history().back().args_u64[0] == 0);
}

// ============================================================================
// T2: destroy_va_space succeeds with valid handle
// ============================================================================

TEST_CASE("destroy_va_space_succeeds_with_valid_handle") {
    MockGpuDriver mock;
    CudaScheduler scheduler(&mock);

    int ret = scheduler.driver()->destroy_va_space(42);

    CHECK(ret == 0);
    CHECK(mock.call_count("destroy_va_space") == 1);
    CHECK(mock.history().back().args_u64[0] == 42);
}

// ============================================================================
// T3: register_gpu succeeds with valid va_space
// ============================================================================

TEST_CASE("register_gpu_succeeds_with_valid_va_space") {
    MockGpuDriver mock;
    CudaScheduler scheduler(&mock);

    int ret = scheduler.driver()->register_gpu(1, 0, 0);

    CHECK(ret == 0);
    CHECK(mock.call_count("register_gpu") == 1);
    CHECK(mock.history().back().args_u64[0] == 1);
    CHECK(mock.history().back().args_u64[1] == 0);
    CHECK(mock.history().back().args_u64[2] == 0);
}

// ============================================================================
// T4: create_queue returns u64 handle
// ============================================================================

TEST_CASE("create_queue_returns_u64_handle") {
    MockGpuDriver mock;
    mock.set_canned_return("create_queue", 0x100000001ULL);
    CudaScheduler scheduler(&mock);

    uint64_t handle = scheduler.driver()->create_queue(1, 0, 50, 4096);

    CHECK(handle == 0x100000001ULL);
    CHECK(handle >= 1);
    CHECK(mock.call_count("create_queue") == 1);
    CHECK(mock.history().back().args_u64[0] == 1);
    CHECK(mock.history().back().args_u64[1] == 0);
    CHECK(mock.history().back().args_u64[2] == 50);
    CHECK(mock.history().back().args_u64[3] == 4096);
}

// ============================================================================
// T5: destroy_queue succeeds with valid handle
// ============================================================================

TEST_CASE("destroy_queue_succeeds_with_valid_handle") {
    MockGpuDriver mock;
    CudaScheduler scheduler(&mock);

    int ret = scheduler.driver()->destroy_queue(42);

    CHECK(ret == 0);
    CHECK(mock.call_count("destroy_queue") == 1);
    CHECK(mock.history().back().args_u64[0] == 42);
}

// ============================================================================
// T6: create_va_space guard verification (H-3.5)
// ============================================================================
//
// MockGpuDriver should now check is_open_ state (consistent with GpuDriverClient).
// When is_open_==false, create_va_space returns 0 without recording the call.

TEST_CASE("create_va_space_guard_when_closed") {
    MockGpuDriver mock;
    mock.inject_error("is_open", true);  // Simulate closed driver state
    CudaScheduler scheduler(&mock);

    uint64_t handle = scheduler.driver()->create_va_space(0);

    CHECK(handle == 0);  // Guard returns 0 (not canned default)
    CHECK(mock.call_count("is_open") == 1);  // is_open was checked
}

// ============================================================================
// T7: destroy_va_space guard verification (H-3.5)
// ============================================================================
//
// MockGpuDriver should now reject handle==0 (consistent with GpuDriverClient/CudaStub).
// Per H-3 spec L52-57: destroy_va_space(0) returns -1, no log.

TEST_CASE("destroy_va_space_guard_when_handle_zero") {
    MockGpuDriver mock;
    CudaScheduler scheduler(&mock);

    int ret = scheduler.driver()->destroy_va_space(0);

    CHECK(ret == -1);  // Guard returns -1 (not canned default 0)
}

// ============================================================================
// T8: register_gpu guard verification (H-3.5)
// ============================================================================
//
// MockGpuDriver should now reject va_space_handle==0 (consistent with GpuDriverClient/CudaStub).
// Per H-3 spec L70-73: register_gpu(0, ...) returns -1, no log.

TEST_CASE("register_gpu_guard_when_va_space_zero") {
    MockGpuDriver mock;
    CudaScheduler scheduler(&mock);

    int ret = scheduler.driver()->register_gpu(0, 0, 0);

    CHECK(ret == -1);  // Guard returns -1 (not canned default 0)
}

// ============================================================================
// T9: create_queue guard verification (H-3.5)
// ============================================================================
//
// MockGpuDriver should now reject va_space_handle==0 (consistent with GpuDriverClient/CudaStub).
// Per H-3 spec L93-96: create_queue(0, ...) returns 0, no log.

TEST_CASE("create_queue_guard_when_va_space_zero") {
    MockGpuDriver mock;
    CudaScheduler scheduler(&mock);

    uint64_t handle = scheduler.driver()->create_queue(0, 0, 50, 4096);

    CHECK(handle == 0);  // Guard returns 0
}

// ============================================================================
// T10: R2 mapping contract — stream_id = LOW32(queue_handle)
// ============================================================================

TEST_CASE("r2_mapping_stream_id_equals_low32_of_queue_handle") {
    MockGpuDriver mock;
    mock.set_canned_return("create_queue", 0x100000001ULL);
    CudaScheduler scheduler(&mock);

    uint64_t queue_handle = scheduler.driver()->create_queue(1, 0, 50, 4096);
    CHECK(queue_handle == 0x100000001ULL);

    uint32_t stream_id = static_cast<uint32_t>(queue_handle);

    scheduler.driver()->submit_batch(stream_id, nullptr, 0, 0);

    CHECK(mock.call_count("submit_batch") == 1);
    CHECK(mock.history().back().method == "submit_batch");
    CHECK(mock.history().back().args_u64[0] == 1);
}

// ============================================================================
// T10b: R2 mapping 截断丢失高 32 位 (契约违反场景)
// ============================================================================

TEST_CASE("r2_mapping_truncation_loses_upper_bits") {
    MockGpuDriver mock;
    mock.set_canned_return("create_queue", 0x100000001ULL);
    CudaScheduler scheduler(&mock);

    uint64_t queue_handle = scheduler.driver()->create_queue(1, 0, 50, 4096);
    uint32_t truncated = static_cast<uint32_t>(queue_handle);
    CHECK(truncated == 1);

    scheduler.driver()->destroy_queue(truncated);
    CHECK(mock.call_count("destroy_queue") == 1);
    CHECK(mock.history().back().args_u64[0] == 1);
}

// ============================================================================
// T10c: R2 mapping 自定义计数器偏离 LOW32 (契约违反场景)
// ============================================================================

TEST_CASE("r2_mapping_custom_counter_diverges") {
    MockGpuDriver mock;
    mock.set_canned_return("create_queue", 0x100000001ULL);
    CudaScheduler scheduler(&mock);

    uint64_t queue_handle = scheduler.driver()->create_queue(1, 0, 50, 4096);
    (void)queue_handle;
    uint32_t custom_counter = 42;

    scheduler.driver()->submit_batch(custom_counter, nullptr, 0, 0);
    CHECK(mock.call_count("submit_batch") == 1);
    CHECK(mock.history().back().args_u64[0] == 42);
}