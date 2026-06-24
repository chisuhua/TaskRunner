// SCOPE: SHARED
/**
 * igpu_driver.hpp - 统一 GPU 驱动抽象接口 (H-2.5)
 *
 * 为 GpuDriverClient (System C ioctl 封装) 和 CudaStub (CUDA Driver API 封装)
 * 提供统一的抽象接口，使 CudaScheduler 等下游组件可以透明地使用不同的
 * GPU 后端实现。
 *
 * 设计目标:
 * - 解耦 GpuDriverClient 与 CudaScheduler
 * - 为 H-3 Phase 2 (VA Space/Queue 显式管理) 预留接口
 * - 统一 snake_case 命名规范 (H-1 closeout)
 *
 * 实现者:
 * - async_task::gpu::GpuDriverClient (System C: GPU_IOCTL_*)
 * - taskrunner::CudaStub            (CUDA Driver API / stub 模式)
 *
 * 创建日期: 2026-06-22
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <iostream>

// System C 接口 (通过符号链接访问)
#include "UsrLinuxEmu/plugins/gpu_driver/shared/gpu_ioctl.h"
#include "UsrLinuxEmu/plugins/gpu_driver/shared/gpu_types.h"

namespace async_task {
namespace gpu {

/**
 * IGpuDriver - 统一 GPU 驱动抽象接口
 *
 * 本接口定义了 TaskRunner 与 GPU 驱动交互所需的全部操作。
 * 所有方法为纯虚函数，具体实现类必须实现全部方法。
 * 命名采用 snake_case 规范。
 *
 * 生命周期:
 *   open() -> 各种操作 -> close()
 *
 * 线程安全: 由具体实现类决定，接口本身不做保证。
 */
class IGpuDriver {
public:
    // ============================================================
    // 核心生命周期 (3)
    // ============================================================

    /**
     * @brief 打开 GPU 设备
     * @return 0 成功，-1 失败
     */
    virtual int open() = 0;

    /**
     * @brief 关闭 GPU 设备
     * @return 0 成功，-1 失败
     */
    virtual int close() = 0;

    /**
     * @brief 检查设备是否已打开
     * @return true 已打开，false 未打开
     */
    virtual bool is_open() const = 0;

    // ============================================================
    // FD 访问 (1)
    // ============================================================

    /**
     * @brief 获取设备文件描述符
     * @return fd 文件描述符，未打开时为 -1
     */
    virtual int fd() const = 0;

    // ============================================================
    // 设备信息 (8)
    // ============================================================

    /**
     * @brief 获取设备信息
     * @param[out] out 设备信息结构体
     * @return 0 成功，-1 失败
     */
    virtual int get_device_info(struct gpu_device_info* out) = 0;

    /**
     * @brief 获取 Warp 大小
     * @return Warp 大小 (NVIDIA=32, AMD CDNA=64, AMD RDNA=32)
     */
    virtual uint32_t get_warp_size() = 0;

    /**
     * @brief 获取 SIMD 单元数量
     * @return SIMD 数量 (AMD CU 或 NVIDIA SM)
     */
    virtual uint32_t get_simd_count() = 0;

    /**
     * @brief 获取峰值 FP32 性能
     * @return 峰值 FP32 GFLOPS
     */
    virtual uint32_t get_peak_fp32_gflops() = 0;

    /**
     * @brief 获取最大引擎时钟频率
     * @return 最大时钟频率 (MHz)
     */
    virtual uint32_t get_max_clock_frequency() = 0;

    /**
     * @brief 获取驱动版本字符串
     * @param[out] out 输出缓冲区
     * @param size 缓冲区大小
     * @return 0 成功，-1 失败
     */
    virtual int get_driver_version_string(char* out, size_t size) = 0;

    /**
     * @brief 获取市场营销名称
     * @param[out] out 输出缓冲区
     * @param size 缓冲区大小
     * @return 0 成功，-1 失败
     */
    virtual int get_marketing_name(char* out, size_t size) = 0;

    /**
     * @brief 打印完整设备信息 (调试用)
     * @param os 输出流，默认 std::cout
     */
    virtual void print_device_info(std::ostream& os = std::cout) = 0;

    // ============================================================
    // 缓冲区对象 (4)
    // ============================================================

    /**
     * @brief 分配 GPU 缓冲区对象
     * @param size 分配大小（字节）
     * @param flags 分配标志 (GPU_BO_DEVICE_LOCAL / GPU_BO_HOST_VISIBLE)
     * @return bo_handle (>=1) 成功，0 失败
     */
    virtual uint64_t alloc_bo(uint64_t size, uint32_t flags) = 0;

    /**
     * @brief 分配 VRAM 缓冲区对象 (DEVICE_LOCAL)
     * @param size 分配大小（字节）
     * @param flags 分配标志
     * @return bo_handle (>=1) 成功，0 失败
     */
    virtual uint64_t alloc_bo_vram(uint64_t size, uint32_t flags) = 0;

    /**
     * @brief 释放 GPU 缓冲区对象
     * @param bo_handle 要释放的 BO handle
     * @return 0 成功，-1 失败
     */
    virtual int free_bo(uint64_t bo_handle) = 0;

    /**
     * @brief 映射 GPU 缓冲区对象到 CPU 虚拟地址
     * @param bo_handle BO handle
     * @param size 映射大小（字节）
     * @return CPU 虚拟地址，失败返回 nullptr
     */
    virtual void* map_bo(uint64_t bo_handle, uint64_t size) = 0;

    // ============================================================
    // 提交 (3, 返回 fence_id)
    // ============================================================

    /**
     * @brief 提交 GPFIFO 命令批次
     * @param stream_id 流/队列 ID
     * @param entries GPFIFO 条目数组
     * @param count 条目数量
     * @param flags 提交标志 (GPU_SUBMIT_FENCE 等)
     * @return fence_id (>=1) 如果包含 FENCE 操作，0 表示成功但无 fence，-1 表示错误
     */
    virtual int64_t submit_batch(uint32_t stream_id,
                                 const struct gpu_gpfifo_entry* entries,
                                 uint32_t count, uint32_t flags) = 0;

    /**
     * @brief 提交单个 MEMCPY 命令 (h2d 或 d2h)
     * @param stream_id 流 ID
     * @param src_addr 源地址
     * @param dst_addr 目的地址
     * @param size 拷贝大小
     * @param is_h2d true 表示 h2d，false 表示 d2h
     * @return fence_id (>=1)，0 表示成功但无 fence，-1 表示错误
     */
    virtual int64_t submit_memcpy(uint32_t stream_id, uint64_t src_addr,
                                  uint64_t dst_addr, uint64_t size, bool is_h2d) = 0;

    /**
     * @brief 提交单个 KERNEL LAUNCH 命令
     * @param stream_id 流 ID
     * @param kernel_index 内核表索引
     * @param grid_x Grid X 维度
     * @param grid_y Grid Y 维度
     * @param grid_z Grid Z 维度
     * @param block_x Block X 维度
     * @param block_y Block Y 维度
     * @param block_z Block Z 维度
     * @return fence_id (>=1)，0 表示成功但无 fence，-1 表示错误
     */
    virtual int64_t submit_launch(uint32_t stream_id, uint32_t kernel_index,
                                  uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
                                  uint32_t block_x, uint32_t block_y,
                                  uint32_t block_z) = 0;

    // ============================================================
    // Fence 等待 (2 重载)
    // ============================================================

    /**
     * @brief 等待 Fence (带超时)
     * @param fence_id 要等待的 Fence ID
     * @param timeout_ms 超时时间（毫秒），0 = 无限等待
     * @param[out] status_out 1=signaled, 0=timeout, -1=error
     * @return 0 成功，-1 失败
     */
    virtual int wait_fence(uint64_t fence_id, uint32_t timeout_ms,
                           uint32_t* status_out) = 0;

    /**
     * @brief 无限等待 Fence (timeout=0)
     * @param fence_id 要等待的 Fence ID
     * @return 0 成功，-1 失败
     */
    virtual int wait_fence(uint64_t fence_id) = 0;

    // ============================================================
    // H-1 VA Space 透传 (2, snake_case 迁移)
    // ============================================================

    /**
     * @brief 设置当前 VA Space handle
     *
     * 透传给后续 submit_batch() 调用。设为 0 时，H-1 校验
     * (validate VA Space exists + validate Queue belongs to VA Space)
     * 将被跳过（向后兼容 sentinel）。调用方应在创建 VA Space 后立即设置。
     *
     * @param va_space_handle VA Space handle (0 = 跳过校验)
     */
    virtual void set_current_va_space(uint64_t va_space_handle) = 0;

    /**
     * @brief 获取当前 VA Space handle
     * @return VA Space handle (0 表示未设置)
     */
    virtual uint64_t get_current_va_space() const = 0;

    // ============================================================
    // H-3 Phase 2 (5, 纯虚占位, 实现延后到 H-3)
    // ============================================================

    /**
     * @brief 创建 GPU 虚拟地址空间 (H-3 Phase 2)
     * @param flags VA Space flags
     * @return VA Space handle (>=1) 成功，0 失败
     */
    virtual uint64_t create_va_space(uint32_t flags) = 0;

    /**
     * @brief 销毁 GPU 虚拟地址空间 (H-3 Phase 2)
     * @param va_space_handle VA Space handle
     * @return 0 成功，-1 失败
     */
    virtual int destroy_va_space(uint64_t va_space_handle) = 0;

    /**
     * @brief 注册 GPU 到 VA Space (H-3 Phase 2, 多 GPU/P2P 场景)
     * @param va_space_handle VA Space handle
     * @param gpu_id GPU ID
     * @param flags 注册标志
     * @return 0 成功，-1 失败
     */
    virtual int register_gpu(uint64_t va_space_handle, uint32_t gpu_id,
                             uint32_t flags) = 0;

    /**
     * @brief 创建 GPU 命令队列 (H-3 Phase 2)
     * @param va_space_handle VA Space handle
     * @param queue_type 队列类型 (GPU_QUEUE_COMPUTE/COPY/GRAPHICS)
     * @param priority 队列优先级 0-100
     * @param ring_buffer_size Ring buffer 大小（条目数）
     * @return queue_handle (>=1) 成功，0 失败
     */
    virtual uint64_t create_queue(uint64_t va_space_handle, uint32_t queue_type,
                                  uint32_t priority, uint64_t ring_buffer_size) = 0;

    /**
     * @brief 销毁 GPU 命令队列 (H-3 Phase 2)
     * @param queue_handle Queue handle
     * @return 0 成功，-1 失败
     */
    virtual int destroy_queue(uint64_t queue_handle) = 0;

    /**
     * @brief 虚析构函数 (允许通过基类指针安全 delete)
     */
    virtual ~IGpuDriver() = default;
};

}  // namespace gpu
}  // namespace async_task
