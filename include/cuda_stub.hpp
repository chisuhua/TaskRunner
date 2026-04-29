/**
 * cuda_stub.hpp - CUDA Driver API 封装
 * 
 * DDS v1.2 架构定义：
 * - 封装 CUDA Driver API (cuMemAlloc, cuLaunchKernel, etc.)
 * - 提供统一的错误处理
 * - Phase 1: 支持基本内存和 Kernel 操作
 */

#ifndef TASKRUNNER_CUDA_STUB_HPP
#define TASKRUNNER_CUDA_STUB_HPP

#include <string>
#include <cstdint>
#include <cstddef>
#include <map>
#include <mutex>
#include <atomic>

// CUDA Driver API 类型前向声明
// 实际使用时需要 #include <cuda.h> 或动态加载
typedef void* CUdeviceptr;
typedef void* CUcontext;
typedef void* CUmodule;
typedef void* CUfunction;
typedef void* CUevent;
typedef void* CUstream;

namespace taskrunner {

/**
 * CUDA 错误码封装
 */
enum class CudaResult {
    SUCCESS = 0,
    ERROR_INVALID_VALUE = 1,
    ERROR_OUT_OF_MEMORY = 2,
    ERROR_NOT_INITIALIZED = 3,
    ERROR_UNKNOWN = 999
};

/**
 * 转换 CUDA 错误码为字符串
 */
const char* cuda_result_to_string(CudaResult result);

/**
 * LaunchParams - Kernel 启动参数
 */
struct LaunchParams {
    const char* kernel_name{nullptr};
    void* params{nullptr};
    size_t params_size{0};
    
    uint32_t grid_dim_x{1};
    uint32_t grid_dim_y{1};
    uint32_t grid_dim_z{1};
    
    uint32_t block_dim_x{256};
    uint32_t block_dim_y{1};
    uint32_t block_dim_z{1};
    
    uint32_t shared_mem_bytes{0};
};

/**
 * CudaStub - CUDA Driver API 封装类
 * 
 * 职责：
 * 1. 管理 CUDA context
 * 2. 封装 cuMemAlloc/cuMemFree
 * 3. 封装 cuMemcpyH2D/cuMemcpyD2H/cuMemcpyD2D
 * 4. 封装 cuLaunchKernel
 * 5. 封装 CUevent 创建和同步
 */
class CudaStub {
public:
    CudaStub();
    ~CudaStub();
    
    // 禁止拷贝
    CudaStub(const CudaStub&) = delete;
    CudaStub& operator=(const CudaStub&) = delete;
    
    // ========== 初始化 ==========
    
    /**
     * 初始化 CUDA
     * @return CudaResult::SUCCESS 或错误码
     */
    CudaResult initialize();
    
    /**
     * 关闭 CUDA
     */
    void shutdown();
    
    /**
     * 检查是否已初始化
     */
    bool is_initialized() const { return initialized_; }
    
    // ========== 内存管理 ==========
    
    /**
     * 分配设备内存
     * @param size 分配大小（字节）
     * @param device_ptr 输出：设备指针
     * @return CudaResult
     */
    CudaResult mem_alloc(size_t size, uint64_t* device_ptr);
    
    /**
     * 释放设备内存
     * @param device_ptr 设备指针
     * @return CudaResult
     */
    CudaResult mem_free(uint64_t device_ptr);
    
    /**
     * Host to Device 拷贝
     * @param dst 目标设备指针
     * @param src 源主机指针
     * @param size 拷贝大小
     * @return CudaResult
     */
    CudaResult memcpy_h2d(uint64_t dst, const void* src, size_t size);
    
    /**
     * Device to Host 拷贝
     * @param dst 目标主机指针
     * @param src 源设备指针
     * @param size 拷贝大小
     * @return CudaResult
     */
    CudaResult memcpy_d2h(void* dst, uint64_t src, size_t size);
    
    /**
     * Device to Device 拷贝
     * @param dst 目标设备指针
     * @param src 源设备指针
     * @param size 拷贝大小
     * @return CudaResult
     */
    CudaResult memcpy_d2d(uint64_t dst, uint64_t src, size_t size);
    
    // ========== Kernel 启动 ==========
    
    /**
     * 启动 Kernel
     * @param params 启动参数
     * @param task_id 输出：任务 ID
     * @return CudaResult
     */
    CudaResult launch_kernel(const LaunchParams& params, uint64_t* task_id);
    
    // ========== 同步原语 ==========
    
    /**
     * 创建 Event
     * @param event_id 输出：Event ID
     * @return CudaResult
     */
    CudaResult create_event(uint64_t* event_id);
    
    /**
     * 记录 Event
     * @param event_id Event ID
     * @return CudaResult
     */
    CudaResult record_event(uint64_t event_id);
    
    /**
     * 等待 Event
     * @param event_id Event ID
     * @param timeout_ms 超时时间（毫秒），0 = 无限等待
     * @return CudaResult
     */
    CudaResult wait_event(uint64_t event_id, uint64_t timeout_ms = 0);
    
    /**
     * 查询 Event 状态
     * @param event_id Event ID
     * @param signaled 输出：1 = signaled, 0 = unsignaled
     * @return CudaResult
     */
    CudaResult query_event(uint64_t event_id, int* signaled);
    
    /**
     * 销毁 Event
     * @param event_id Event ID
     * @return CudaResult
     */
    CudaResult destroy_event(uint64_t event_id);
    
    // ========== Stub 模式 ==========
    
    /**
     * 启用 Stub 模式（无真实 GPU 时）
     * @param enable true = 启用 stub
     */
    void set_stub_mode(bool enable) { stub_mode_ = enable; }
    
    /**
     * 检查是否为 Stub 模式
     */
    bool is_stub_mode() const { return stub_mode_; }
    
private:
    bool initialized_{false};
    bool stub_mode_{false};
    
    CUcontext context_{nullptr};
    
    // Event 管理
    std::map<uint64_t, CUevent> events_;
    std::atomic<uint64_t> next_event_id_{1};
    std::mutex events_mutex_;
    
    // Task ID 计数器
    std::atomic<uint64_t> next_task_id_{1};
};

} // namespace taskrunner

#endif // TASKRUNNER_CUDA_STUB_HPP
