/**
 * test_cuda_scheduler.cpp - CudaScheduler 端到端测试
 * 
 * 测试流程：alloc → memcpy_h2d → launch → wait → memcpy_d2h → verify
 * 
 * 使用 doctest 测试框架
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "cuda_scheduler.hpp"
#include <vector>
#include <cstring>
#include <iostream>

using namespace taskrunner;

// ============================================================================
// 测试用例 1: 内存分配与释放
// ============================================================================

TEST_CASE("CudaScheduler: Memory Alloc/Free") {
    CudaScheduler scheduler;
    
    // 初始化（Stub 模式）
    CHECK(scheduler.initialize(true) == 0);
    CHECK(scheduler.is_initialized() == true);
    
    // 分配内存
    auto result = scheduler.submit_mem_alloc(4096);
    CHECK(result.status == 0);
    CHECK(result.device_ptr != 0);
    CHECK(result.fence_id != 0);
    
    // 等待 fence（Stub 模式应立即完成）
    CHECK(scheduler.wait_fence(result.fence_id) == 0);
    
    // 释放内存
    CHECK(scheduler.submit_mem_free(result.device_ptr) == 0);
    
    // 关闭
    scheduler.shutdown();
    CHECK(scheduler.is_initialized() == false);
}

// ============================================================================
// 测试用例 2: Host to Device 内存拷贝
// ============================================================================

TEST_CASE("CudaScheduler: Memcpy H2D") {
    CudaScheduler scheduler;
    CHECK(scheduler.initialize(true) == 0);
    
    // 分配内存
    auto alloc_result = scheduler.submit_mem_alloc(1024);
    CHECK(alloc_result.status == 0);
    
    // 准备测试数据
    std::vector<uint8_t> host_data(256, 0xAB);
    
    // H2D 拷贝
    int fence_id = scheduler.submit_memcpy_h2d(
        alloc_result.device_ptr, 0, host_data.data(), host_data.size());
    CHECK(fence_id > 0);
    
    // 等待完成
    CHECK(scheduler.wait_fence(fence_id) == 0);
    
    // 清理
    scheduler.submit_mem_free(alloc_result.device_ptr);
    scheduler.shutdown();
}

// ============================================================================
// 测试用例 3: Device to Host 内存拷贝
// ============================================================================

TEST_CASE("CudaScheduler: Memcpy D2H") {
    CudaScheduler scheduler;
    CHECK(scheduler.initialize(true) == 0);
    
    // 分配内存
    auto alloc_result = scheduler.submit_mem_alloc(1024);
    CHECK(alloc_result.status == 0);
    
    // 准备接收缓冲区
    std::vector<uint8_t> host_data(256, 0);
    
    // D2H 拷贝
    int fence_id = scheduler.submit_memcpy_d2h(
        host_data.data(), alloc_result.device_ptr, 0, host_data.size());
    CHECK(fence_id > 0);
    
    // 等待完成
    CHECK(scheduler.wait_fence(fence_id) == 0);
    
    // Stub 模式下数据应为 0
    CHECK(std::all_of(host_data.begin(), host_data.end(), [](uint8_t v) { return v == 0; }));
    
    // 清理
    scheduler.submit_mem_free(alloc_result.device_ptr);
    scheduler.shutdown();
}

// ============================================================================
// 测试用例 4: Kernel 启动
// ============================================================================

TEST_CASE("CudaScheduler: Kernel Launch") {
    CudaScheduler scheduler;
    CHECK(scheduler.initialize(true) == 0);
    
    // 准备启动参数
    LaunchParams params;
    params.kernel_name = "vectorAdd";
    params.grid_dim_x = 1;
    params.grid_dim_y = 1;
    params.grid_dim_z = 1;
    params.block_dim_x = 256;
    params.block_dim_y = 1;
    params.block_dim_z = 1;
    
    // 启动 Kernel
    auto result = scheduler.submit_launch(params);
    CHECK(result.status == 0);
    CHECK(result.task_id != 0);
    CHECK(result.fence_id != 0);
    
    // 等待完成（Stub 模式应立即完成）
    CHECK(scheduler.wait_fence(result.fence_id) == 0);
    
    // 查询任务状态
    const Task* task = scheduler.get_task(result.task_id);
    CHECK(task != nullptr);
    CHECK(task->state == Task::State::COMPLETED);
    
    // 清理
    scheduler.shutdown();
}

// ============================================================================
// 测试用例 5: 端到端流程（Vector Add 模拟）
// ============================================================================

TEST_CASE("CudaScheduler: End-to-End Vector Add") {
    CudaScheduler scheduler;
    CHECK(scheduler.initialize(true) == 0);
    
    const size_t N = 256;
    const size_t vec_size = N * sizeof(float);
    
    // ========== Phase 1: 分配设备内存 ==========
    auto d_a = scheduler.submit_mem_alloc(vec_size);
    auto d_b = scheduler.submit_mem_alloc(vec_size);
    auto d_c = scheduler.submit_mem_alloc(vec_size);
    
    CHECK(d_a.status == 0);
    CHECK(d_b.status == 0);
    CHECK(d_c.status == 0);
    
    std::cout << "[E2E] Allocated device memory:\n";
    std::cout << "  d_a = 0x" << std::hex << d_a.device_ptr << std::dec << "\n";
    std::cout << "  d_b = 0x" << std::hex << d_b.device_ptr << std::dec << "\n";
    std::cout << "  d_c = 0x" << std::hex << d_c.device_ptr << std::dec << "\n";
    
    // ========== Phase 2: 准备主机数据 ==========
    std::vector<float> h_a(N), h_b(N), h_c(N);
    for (size_t i = 0; i < N; i++) {
        h_a[i] = static_cast<float>(i);
        h_b[i] = static_cast<float>(i * 2);
    }
    
    // ========== Phase 3: H2D 拷贝 ==========
    int fence_a = scheduler.submit_memcpy_h2d(d_a.device_ptr, 0, h_a.data(), vec_size);
    int fence_b = scheduler.submit_memcpy_h2d(d_b.device_ptr, 0, h_b.data(), vec_size);
    
    CHECK(fence_a > 0);
    CHECK(fence_b > 0);
    
    CHECK(scheduler.wait_fence(fence_a) == 0);
    CHECK(scheduler.wait_fence(fence_b) == 0);
    
    std::cout << "[E2E] Copied input data to device\n";
    
    // ========== Phase 4: 启动 Kernel ==========
    LaunchParams params;
    params.kernel_name = "vectorAdd";
    params.grid_dim_x = (N + 255) / 256;
    params.grid_dim_y = 1;
    params.grid_dim_z = 1;
    params.block_dim_x = 256;
    params.block_dim_y = 1;
    params.block_dim_z = 1;
    
    auto launch_result = scheduler.submit_launch(params);
    CHECK(launch_result.status == 0);
    CHECK(scheduler.wait_fence(launch_result.fence_id) == 0);
    
    std::cout << "[E2E] Launched kernel 'vectorAdd' (task_id=" << launch_result.task_id << ")\n";
    
    // ========== Phase 5: D2H 拷贝 ==========
    int fence_c = scheduler.submit_memcpy_d2h(h_c.data(), d_c.device_ptr, 0, vec_size);
    CHECK(fence_c > 0);
    CHECK(scheduler.wait_fence(fence_c) == 0);
    
    std::cout << "[E2E] Copied result back to host\n";
    
    // ========== Phase 6: 验证结果（Stub 模式结果为 0） ==========
    // Stub 模式下，实际 GPU 未执行，结果为 0
    // Phase 2 真实 GPU 测试时，应验证 h_c[i] == h_a[i] + h_b[i]
    std::cout << "[E2E] Result verification (Stub mode returns zeros):\n";
    std::cout << "  h_c[0..7]: ";
    for (int i = 0; i < 8; i++) {
        std::printf("%.1f ", h_c[i]);
    }
    std::printf("\n");
    
    // ========== Phase 7: 清理 ==========
    scheduler.submit_mem_free(d_a.device_ptr);
    scheduler.submit_mem_free(d_b.device_ptr);
    scheduler.submit_mem_free(d_c.device_ptr);
    scheduler.shutdown();
    
    std::cout << "[E2E] Test completed successfully\n";
}

// ============================================================================
// 测试用例 6: Fence 查询
// ============================================================================

TEST_CASE("CudaScheduler: Fence Query") {
    CudaScheduler scheduler;
    CHECK(scheduler.initialize(true) == 0);
    
    // 分配内存（会创建 fence）
    auto result = scheduler.submit_mem_alloc(1024);
    CHECK(result.status == 0);
    
    // Stub 模式下 fence 应立即完成
    int signaled = scheduler.query_fence(result.fence_id);
    CHECK(signaled == 1);
    
    // 清理
    scheduler.submit_mem_free(result.device_ptr);
    scheduler.shutdown();
}

// ============================================================================
// 测试用例 7: 错误处理 - 无效参数
// ============================================================================

TEST_CASE("CudaScheduler: Error Handling - Invalid Params") {
    CudaScheduler scheduler;
    CHECK(scheduler.initialize(true) == 0);
    
    // 未初始化时操作
    CudaScheduler uninit_scheduler;
    auto bad_alloc = uninit_scheduler.submit_mem_alloc(1024);
    CHECK(bad_alloc.status == -ENOTCONN);
    
    // 释放空指针
    CHECK(scheduler.submit_mem_free(0) == -EINVAL);
    
    // 查询不存在的 fence
    CHECK(scheduler.query_fence(99999) == -ENOENT);
    
    scheduler.shutdown();
}

// ============================================================================
// 测试用例 8: 内存统计
// ============================================================================

TEST_CASE("CudaScheduler: Memory Statistics") {
    CudaScheduler scheduler;
    CHECK(scheduler.initialize(true) == 0);
    
    // 初始状态
    CHECK(scheduler.memory_manager().total_allocated() == 0);
    CHECK(scheduler.memory_manager().allocation_count() == 0);
    
    // 分配后
    auto r1 = scheduler.submit_mem_alloc(1024);
    auto r2 = scheduler.submit_mem_alloc(2048);
    
    CHECK(scheduler.memory_manager().allocation_count() == 2);
    CHECK(scheduler.memory_manager().total_allocated() == 3072);
    
    // 释放后
    scheduler.submit_mem_free(r1.device_ptr);
    CHECK(scheduler.memory_manager().allocation_count() == 1);
    CHECK(scheduler.memory_manager().total_allocated() == 2048);
    
    scheduler.shutdown();
}
