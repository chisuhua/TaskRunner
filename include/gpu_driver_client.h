/**
 * gpu_driver_client.h - System C (GPU_IOCTL_*) 封装层
 *
 * 提供 TaskRunner 到 UsrLinuxEmu GPU 驱动的客户端接口。
 * 基于 ADR-015 和同步点 S0-S3 确认的接口。
 *
 * 符号链接: TaskRunner/UsrLinuxEmu → ../UsrLinuxEmu/
 * 接口定义: UsrLinuxEmu/plugins/gpu_driver/shared/gpu_ioctl.h
 *
 * 创建日期: 2026-04-29
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <cerrno>
#include <iostream>

// System C 接口 (通过符号链接访问)
#include "UsrLinuxEmu/plugins/gpu_driver/shared/gpu_ioctl.h"
#include "UsrLinuxEmu/plugins/gpu_driver/shared/gpu_types.h"

namespace async_task {
namespace gpu {

/**
 * GPU 设备客户端封装类
 *
 * 提供所有 GPU 操作的高层接口，封装 ioctl 调用细节。
 * 支持 System C (GPU_IOCTL_*) 接口。
 */
class GpuDriverClient {
public:
    /**
     * 构造函数
     * @param device_path GPU 设备节点路径，默认为 /dev/gpgpu0
     */
    explicit GpuDriverClient(const char* device_path = "/dev/gpgpu0")
        : fd_(-1), device_path_(device_path) {}

    /**
     * 析构函数 - 关闭设备文件描述符
     */
    ~GpuDriverClient() {
        close();
    }

    // 禁止拷贝 - 防止double-close bug
    GpuDriverClient(const GpuDriverClient&) = delete;
    GpuDriverClient& operator=(const GpuDriverClient&) = delete;

    /**
     * 打开 GPU 设备
     * @return 0 成功，-1 失败
     */
    int open() {
        if (fd_ >= 0) {
            return 0;  // 已经打开
        }
        // 使用 O_NOFOLLOW 防止 symlink 攻击
        fd_ = ::open(device_path_.c_str(), O_RDWR | O_NOFOLLOW);
        if (fd_ < 0) {
            std::cerr << "GpuDriverClient: Failed to open GPU device"
                      << " (errno=" << errno << ")\n";
            return -1;
        }
        return 0;
    }

    /**
     * 关闭 GPU 设备
     */
    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    /**
     * 检查设备是否已打开
     */
    bool is_open() const { return fd_ >= 0; }

    /**
     * 获取设备文件描述符
     */
    int fd() const { return fd_; }

    // ============================================================
    // GPU_IOCTL_GET_DEVICE_INFO
    // ============================================================

    /**
     * 获取设备信息
     * @param info 输出参数，设备信息结构体
     * @return 0 成功，-1 失败
     */
    int get_device_info(struct gpu_device_info* info) {
        if (!is_open()) return -1;
        if (ioctl(fd_, GPU_IOCTL_GET_DEVICE_INFO, info) < 0) {
            std::cerr << "GpuDriverClient: GPU_IOCTL_GET_DEVICE_INFO failed"
                      << " (errno=" << errno << ")\n";
            return -1;
        }
        return 0;
    }

    // ============================================================
    // GPU_IOCTL_ALLOC_BO
    // ============================================================

    /**
     * 分配 GPU 缓冲区对象
     * @param size 分配大小（字节）
     * @param domain 内存域 (VRAM/GTT/CPU)
     * @param flags 分配标志 (DEVICE_LOCAL/HOST_VISIBLE)
     * @param handle 输出参数，返回的 GEM handle
     * @param gpu_va 输出参数，返回的 GPU 虚拟地址
     * @return 0 成功，-1 失败
     */
    int alloc_bo(uint64_t size, uint32_t domain, uint32_t flags,
                  uint32_t* handle, uint64_t* gpu_va) {
        if (!is_open()) return -1;
        if (size == 0) return -1;

        struct gpu_alloc_bo_args args = {};
        args.size = size;
        args.domain = domain;
        args.flags = flags;

        if (ioctl(fd_, GPU_IOCTL_ALLOC_BO, &args) < 0) {
            std::cerr << "GpuDriverClient: GPU_IOCTL_ALLOC_BO failed"
                      << " (errno=" << errno << ")\n";
            return -1;
        }

        *handle = args.handle;
        *gpu_va = args.gpu_va;
        return 0;
    }

    /**
     * 简化版分配 - 使用 VRAM + DEVICE_LOCAL
     */
    int alloc_bo_vram(uint64_t size, uint32_t* handle, uint64_t* gpu_va) {
        return alloc_bo(size, GPU_MEM_DOMAIN_VRAM, GPU_BO_DEVICE_LOCAL,
                        handle, gpu_va);
    }

    // ============================================================
    // GPU_IOCTL_FREE_BO
    // ============================================================

    /**
     * 释放 GPU 缓冲区对象
     * @param handle 要释放的 GEM handle
     * @return 0 成功，-1 失败
     */
    int free_bo(uint32_t handle) {
        if (!is_open()) return -1;
        if (handle == 0) {
            std::cerr << "GpuDriverClient: free_bo called with handle=0\n";
            return -1;
        }

        if (ioctl(fd_, GPU_IOCTL_FREE_BO, &handle) < 0) {
            std::cerr << "GpuDriverClient: GPU_IOCTL_FREE_BO failed"
                      << " (errno=" << errno << ")\n";
            return -1;
        }
        return 0;
    }

    // ============================================================
    // GPU_IOCTL_MAP_BO
    // ============================================================

    /**
     * 映射 GPU 缓冲区对象
     * @param handle GEM handle
     * @param flags 映射标志
     * @param gpu_va 输出参数，返回的 GPU 虚拟地址
     * @return 0 成功，-1 失败
     */
    int map_bo(uint32_t handle, uint32_t flags, uint64_t* gpu_va) {
        if (!is_open()) return -1;
        if (handle == 0) return -1;

        struct gpu_map_bo_args args = {};
        args.handle = handle;
        args.flags = flags;

        if (ioctl(fd_, GPU_IOCTL_MAP_BO, &args) < 0) {
            std::cerr << "GpuDriverClient: GPU_IOCTL_MAP_BO failed"
                      << " (errno=" << errno << ")\n";
            return -1;
        }

        *gpu_va = args.gpu_va;
        return 0;
    }

    // ============================================================
    // GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH
    // ============================================================

    /**
     * 提交 GPFIFO 命令批次
     * @param stream_id 流/队列 ID
     * @param entries GPFIFO 条目数组
     * @param count 条目数量
     * @param flags 提交标志 (GPU_SUBMIT_FENCE 等)
     * @return fence_id (>=1) 如果包含 FENCE 操作，0 表示成功但无 fence，-1 表示错误
     */
    int64_t submit_batch(uint32_t stream_id, const struct gpu_gpfifo_entry* entries,
                          uint32_t count, uint32_t flags) {
        if (!is_open()) return -1;
        if (!entries || count == 0) return -1;

        struct gpu_pushbuffer_args args = {};
        args.stream_id = stream_id;
        args.entries = entries;
        args.count = count;
        args.flags = flags;
        args.fence_id = 0;  // 初始化

        if (ioctl(fd_, GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH, &args) < 0) {
            std::cerr << "GpuDriverClient: GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH failed"
                      << " (errno=" << errno << ")\n";
            return -1;
        }
        return static_cast<int64_t>(args.fence_id);  // 返回 fence_id (可能是0)
    }

    /**
     * 提交单个 MEMCPY 命令 (h2d 或 d2h)
     * @param stream_id 流 ID
     * @param src_addr 源地址
     * @param dst_addr 目的地址
     * @param size 拷贝大小
     * @param is_h2d true 表示 h2d，false 表示 d2h
     * @return fence_id (>=1)，0 表示成功但无 fence，-1 表示错误
     */
    int64_t submit_memcpy(uint32_t stream_id, uint64_t src_addr, uint64_t dst_addr,
                           uint64_t size, bool is_h2d) {
        if (size == 0) return -1;

        struct gpu_gpfifo_entry entry = {};
        entry.valid = 1;
        entry.priv = 0;
        entry.method = GPU_OP_MEMCPY;  // 0x102
        entry.subchannel = 0;

        // h2d: payload[0]=host_ptr, payload[1]=gpu_ptr
        // d2h: payload[0]=gpu_ptr, payload[1]=host_ptr
        entry.payload[0] = src_addr;
        entry.payload[1] = dst_addr;
        entry.payload[2] = size;
        entry.payload[3] = 0;
        entry.payload[4] = 0;
        entry.payload[5] = 0;
        entry.payload[6] = 0;

        entry.semaphore_va = 0;
        entry.semaphore_value = 0;
        entry.release = 0;

        return submit_batch(stream_id, &entry, 1, GPU_SUBMIT_FENCE);
    }

    /**
     * 提交单个 KERNEL LAUNCH 命令
     * @param stream_id 流 ID
     * @param kernel_index 内核表索引
     * @param grid_x/y/z Grid 维度
     * @param block_x/y/z Block 维度
     * @return fence_id (>=1)，0 表示成功但无 fence，-1 表示错误
     */
    int64_t submit_launch(uint32_t stream_id, uint32_t kernel_index,
                      uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
                      uint32_t block_x, uint32_t block_y, uint32_t block_z) {
        if (grid_x == 0 || grid_y == 0 || grid_z == 0) return -1;
        if (block_x == 0 || block_y == 0 || block_z == 0) return -1;

        struct gpu_gpfifo_entry entry = {};
        entry.valid = 1;
        entry.priv = 0;
        entry.method = GPU_OP_LAUNCH_KERNEL;  // 0x100
        entry.subchannel = 0;

        // payload[0] = kernel_table_index
        entry.payload[0] = kernel_index;

        // payload[1] = grid_dim (压缩: grid_x | (grid_y << 16) | (grid_z << 24))
        entry.payload[1] = grid_x | (grid_y << 16) | (static_cast<uint64_t>(grid_z) << 24);

        // payload[2] = block_dim (压缩: block_x | (block_y << 8) | (block_z << 16))
        entry.payload[2] = block_x | (block_y << 8) | (static_cast<uint64_t>(block_z) << 16);

        entry.payload[3] = 0;
        entry.payload[4] = 0;
        entry.payload[5] = 0;
        entry.payload[6] = 0;

        entry.semaphore_va = 0;
        entry.semaphore_value = 0;
        entry.release = 0;

        return submit_batch(stream_id, &entry, 1, GPU_SUBMIT_FENCE);
    }

    // ============================================================
    // GPU_IOCTL_WAIT_FENCE
    // ============================================================

    /**
     * 等待 Fence
     * @param fence_id 要等待的 Fence ID
     * @param timeout_ms 超时时间（毫秒），0 = 无限等待
     * @param status 输出参数，1=signaled, 0=timeout, -1=error
     * @return 0 成功，-1 失败
     */
    int wait_fence(uint64_t fence_id, uint32_t timeout_ms, uint32_t* status) {
        if (!is_open()) return -1;
        if (fence_id == 0) return -1;

        struct gpu_wait_fence_args args = {};
        args.fence_id = fence_id;
        args.timeout_ms = timeout_ms;

        if (ioctl(fd_, GPU_IOCTL_WAIT_FENCE, &args) < 0) {
            std::cerr << "GpuDriverClient: GPU_IOCTL_WAIT_FENCE failed"
                      << " (errno=" << errno << ")\n";
            return -1;
        }

        *status = args.status;
        return 0;
    }

    /**
     * 无限等待 Fence
     */
    int wait_fence(uint64_t fence_id) {
        uint32_t status;
        return wait_fence(fence_id, 0, &status);
    }

private:
    int fd_;                      // 设备文件描述符
    std::string device_path_;      // 设备路径
};

/**
 * 全局 GPU 客户端实例（用于 CLI 模式）
 */
extern GpuDriverClient* g_gpu_client;

/**
 * 初始化全局 GPU 客户端
 * @return 0 成功，-1 失败
 */
int init_gpu_client();

/**
 * 关闭全局 GPU 客户端
 */
void shutdown_gpu_client();

}  // namespace gpu
}  // namespace async_task
