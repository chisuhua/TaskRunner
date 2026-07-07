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

    // ============================================================
    // H-3.5 生命周期扩展 (3, 非纯虚, 默认 no-op/success)
    // ============================================================
    //
    // 这 3 个方法从 CudaStub-specific 行为上移到 IGpuDriver 接口,
    // 让 3 个实现 (GpuDriverClient / CudaStub / MockGpuDriver) 各自 override.
    // 默认实现允许现有代码零修改 (向后兼容).
    //
    // 设计理由 (TADR-109 H-3.5 follow-up):
    // - 之前 CudaScheduler 通过 dynamic_cast<CudaStub*> 调用 CudaStub-specific
    //   set_stub_mode/initialize/shutdown (src/test_fixture/cuda_scheduler.cpp:45, 65)
    // - 上移到 IGpuDriver 后, CudaScheduler 改用 driver_->set_stub_mode/initialize/shutdown
    //   统一抽象调用 (删除 dynamic_cast 抽象泄漏)
    //
    // 4 个 legacy dynamic_cast (mem_alloc/memcpy_*/launch_kernel) 保留,
    // 见 design.md Decision 1 理由 (这些是 CudaStub-only CUDA Driver API 路径,
    // 不适合上移到 IGpuDriver 抽象).

    /**
     * @brief 设置 stub 模式 (仅 CudaStub 使用, GpuDriverClient/MockGpuDriver 默认 no-op)
     * @param stub_mode true = stub 模式 (模拟, 不真实计算), false = 真实模式
     */
    virtual void set_stub_mode(bool stub_mode) {}

    /**
     * @brief 初始化 driver 内部状态
     * @return 0 成功, -1 失败
     */
    virtual int initialize() { return 0; }

    /**
     * @brief 关闭 driver 释放资源
     */
    virtual void shutdown() {}

    // ============================================================
    // Phase 3.1 Stream Capture / Graph (10, 虚函数 默认 no-op, 非纯虚)
    // ============================================================
    //
    // 决策来源 (UsrLinuxEmu Architecture Team 2026-07-05 反馈):
    // - F-1 capture mode 仅接受 GLOBAL (其他 mode 返回 -1, 不静默 fallback)
    // - B-3 fence_id 范围划分 (HAL [1, 1<<32-1] + sim [1<<32, INT64_MAX])
    // - F-3 kernargs_bo_handle=0 表示无 kernargs BO (不校验 BO 表)
    // - F-4 int64_t 返回约定 (<0 = errno, >= (1<<32) = valid fence_id)
    //
    // 兼容性: 所有方法为 虚函数 + 默认 no-op 实现 (非纯虚).
    // CudaStub / MockGpuDriver 不强制 override; GpuDriverClient 在 Step 3
    // 实现 15 forwarding 方法覆盖这些默认实现.

    /**
     * @brief 查询 stream capture 状态 (Phase 3.1)
     * @param stream_id 流 ID
     * @param[out] status_out SIM_STREAM_CAPTURE_STATUS_* (NONE/ACTIVE/INVALID)
     * @return 0 成功, -1 失败
     */
    virtual int stream_capture_status(uint32_t stream_id, uint32_t* status_out) { return -1; }

    /**
     * @brief 开始 stream capture (Phase 3.1)
     * @note UsrLinuxEmu 仅接受 SIM_CAPTURE_MODE_GLOBAL (0); 其他 mode 返回 -1
     * @param stream_id 流 ID
     * @param mode capture mode (CU_STREAM_CAPTURE_MODE_*)
     * @return 0 成功, -1 失败
     */
    virtual int stream_capture_begin(uint32_t stream_id, uint32_t mode) { return -1; }

    /**
     * @brief 结束 stream capture, 返回 graph handle (Phase 3.1)
     * @param stream_id 流 ID
     * @param[out] graph_handle_out 返回的 graph handle (>=1)
     * @return 0 成功, -1 失败
     */
    virtual int stream_capture_end(uint32_t stream_id, uint64_t* graph_handle_out) { return -1; }

    /**
     * @brief 创建空 CUDA graph (Phase 3.1)
     * @param[out] graph_handle_out graph handle (>=1)
     * @return 0 成功, -1 失败
     */
    virtual int graph_create(uint64_t* graph_handle_out) { return -1; }

    /**
     * @brief 销毁 CUDA graph (Phase 3.1)
     * @param graph_handle graph handle
     * @return 0 成功, -1 失败
     */
    virtual int graph_destroy(uint64_t graph_handle) { return -1; }

    /**
     * @brief 向 graph 添加 kernel launch 节点 (Phase 3.1)
     * @note kernargs_bo_handle == 0 表示无参数 kernel, 不校验 BO 表存在性
     * @param graph_handle graph handle
     * @param kernel_index 内核注册表索引
     * @param grid_x, grid_y, grid_z 启动 grid 维度
     * @param block_x, block_y, block_z 启动 block 维度
     * @param kernargs_bo_handle kernargs BO handle (0 = 无)
     * @return 0 成功, -1 失败
     */
    virtual int graph_add_kernel_node(uint64_t graph_handle, uint32_t kernel_index,
                                      uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
                                      uint32_t block_x, uint32_t block_y, uint32_t block_z,
                                      uint64_t kernargs_bo_handle) { return -1; }

    /**
     * @brief 向 graph 添加 memcpy 节点 (Phase 3.1)
     * @param graph_handle graph handle
     * @param src_va 源虚拟地址
     * @param dst_va 目的虚拟地址
     * @param size 拷贝字节数
     * @param is_h2d 1=H2D, 0=D2H
     * @return 0 成功, -1 失败
     */
    virtual int graph_add_memcpy_node(uint64_t graph_handle,
                                      uint64_t src_va, uint64_t dst_va,
                                      uint64_t size, uint32_t is_h2d) { return -1; }

    /**
     * @brief 实例化 graph 为可执行对象 (Phase 3.1)
     * @param graph_handle graph handle
     * @param[out] exec_handle_out executable handle (>=1)
     * @return 0 成功, -1 失败
     */
    virtual int graph_instantiate(uint64_t graph_handle, uint64_t* exec_handle_out) { return -1; }

    /**
     * @brief 提交 graph executable 启动 (Phase 3.1)
     * @note F-4 返回约定: <0 = errno, >= (1<<32) = valid sim fence_id, 0 = 成功无 fence
     * @param graph_exec_handle executable handle
     * @param stream_id 流 ID
     * @return fence_id (>= (1<<32) 表示 sim fence), <0 = errno, -1 = 未实现
     */
    virtual int64_t submit_graph(uint64_t graph_exec_handle, uint32_t stream_id) { return -1; }

    /**
     * @brief 销毁 graph executable (Phase 3.1)
     * @param graph_exec_handle executable handle
     * @return 0 成功, -1 失败
     */
    virtual int destroy_graph_exec(uint64_t graph_exec_handle) { return -1; }

    // ============================================================
    // Phase 3.2 Memory Pool (5, 虚函数 默认 no-op, 非纯虚)
    // ============================================================
    //
    // 决策来源 (UsrLinuxEmu Architecture Team 2026-07-05 反馈):
    // - B-2 Pool VA 范围采用 Option B (VA 子范围预留); sim_mem_pool_props_t
    //   加 va_base / va_limit 字段
    // - F-2 attr value blob 布局 (Step 3 实施, 本 change 不涉及)
    //
    // 兼容性: 所有方法为 虚函数 + 默认 no-op 实现 (非纯虚).
    //
    // 注意: set_attr/get_attr/trim 3 个 IOCTL (0x65-0x67) 不需要单独 IGpuDriver
    // 方法 — Oracle 决策, 通过现有 alloc/destroy 语义覆盖. 若 Phase 3.x 需要
    // 显式属性控制, 加 3 个方法 (Phase 3.2 → 49 total).

    /**
     * @brief 创建 memory pool (Phase 3.2)
     * @note UsrLinuxEmu B-2: pool 预留 maxSize VA 子范围 via sim_mem_pool_props_t
     * @param va_space_handle 所属 VA Space handle
     * @param size pool 总大小 (字节)
     * @param flags pool 标志 (CU_MEMPOOL_*)
     * @param[out] pool_handle_out pool handle (>=1)
     * @return 0 成功, -1 失败
     */
    virtual int mem_pool_create(uint64_t va_space_handle, uint64_t size,
                                uint32_t flags, uint64_t* pool_handle_out) { return -1; }

    /**
     * @brief 销毁 memory pool (Phase 3.2)
     * @param pool_handle pool handle
     * @return 0 成功, -1 失败
     */
    virtual int mem_pool_destroy(uint64_t pool_handle) { return -1; }

    /**
     * @brief 从 pool 同步分配 (Phase 3.2)
     * @param pool_handle pool handle
     * @param size 分配字节数
     * @param[out] va_out 返回的虚拟地址
     * @return 0 成功, -1 失败
     */
    virtual int mem_pool_alloc(uint64_t pool_handle, uint64_t size,
                               uint64_t* va_out) { return -1; }

    /**
     * @brief 从 pool 异步分配 (Phase 3.2)
     * @note F-4 返回约定: <0 = errno, >= (1<<32) = valid sim fence_id
     * @param pool_handle pool handle
     * @param size 分配字节数
     * @param stream_id 目标流
     * @param[out] va_out 返回的虚拟地址
     * @return fence_id (>= (1<<32) 表示 sim fence), <0 = errno, -1 = 未实现
     */
    virtual int64_t mem_pool_alloc_async(uint64_t pool_handle, uint64_t size,
                                         uint32_t stream_id, uint64_t* va_out) { return -1; }

    /**
     * @brief 异步释放 (Phase 3.2)
     * @note F-4 返回约定: <0 = errno, >= (1<<32) = valid sim fence_id
     * @param va 要释放的虚拟地址
     * @param stream_id 目标流
     * @return fence_id (>= (1<<32) 表示 sim fence), <0 = errno, -1 = 未实现
     */
    virtual int64_t mem_pool_free_async(uint64_t va, uint32_t stream_id) { return -1; }

    /**
     * @brief 导出 memory pool 的可共享 handle (Phase 4)
     *
     * 对应 cuMemPoolExportToShareableHandle。将 pool 导出为 POSIX FD，
     * 用于跨进程共享。
     *
     * @param pool_handle pool handle
     * @param handle_type 导出类型 (CU_MEM_HANDLE_TYPE_POSIX_FD = 1)
     * @param flags 保留 (必须为 0)
     * @param[out] fd_out POSIX 文件描述符 (≥0 有效)
     * @return 0 成功, -1 失败 (默认: NOT_SUPPORTED)
     */
    virtual int mem_pool_export_shareable(uint64_t pool_handle,
                                          uint32_t handle_type,
                                          uint32_t flags,
                                          int* fd_out) { return -1; }

    /**
     * @brief 虚析构函数 (允许通过基类指针安全 delete)
     */
    virtual ~IGpuDriver() = default;
};

}  // namespace gpu
}  // namespace async_task
