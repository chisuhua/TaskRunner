/**
 * gpu_driver_client.h - System C (GPU_IOCTL_*) 封装层
 *
 * 提供 TaskRunner 到 UsrLinuxEmu GPU 驱动的客户端接口。
 * 基于 ADR-015 和同步点 S0-S3 确认的接口。
 *
 * H-2.5: GpuDriverClient 实现统一抽象接口 IGpuDriver (28 个方法)
 *  - 4 个 BO 方法按 D6/D7/D8 对齐 (alloc_bo, alloc_bo_vram, free_bo, map_bo)
 *  - H-1 既有 setCurrentVASpace()/getCurrentVASpace() 保留作 deprecated alias
 *  - 5 个 H-3 占位方法 (create_va_space 等) 抛 std::runtime_error
 *
 * 符号链接: TaskRunner/UsrLinuxEmu → ../UsrLinuxEmu/
 * 接口定义: UsrLinuxEmu/plugins/gpu_driver/shared/gpu_ioctl.h
 *
 * 创建日期: 2026-04-29
 * H-2.5 重构: 2026-06-22
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <stdexcept>
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

// 统一抽象接口
#include "igpu_driver.hpp"

namespace async_task {
namespace gpu {

/**
 * GPU 设备客户端封装类
 *
 * 实现 IGpuDriver 接口，对应 System C (GPU_IOCTL_*) 后端。
 */
class GpuDriverClient : public IGpuDriver {
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
    ~GpuDriverClient() override {
        close();
    }

    // 禁止拷贝 - 防止double-close bug
    GpuDriverClient(const GpuDriverClient&) = delete;
    GpuDriverClient& operator=(const GpuDriverClient&) = delete;

    // ============================================================
    // 核心生命周期 (3) - IGpuDriver
    // ============================================================

    /**
     * 打开 GPU 设备
     * @return 0 成功，-1 失败
     */
    int open() override {
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
    int close() override {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        return 0;
    }

    /**
     * 检查设备是否已打开
     */
    bool is_open() const override { return fd_ >= 0; }

    // ============================================================
    // FD 访问 (1) - IGpuDriver
    // ============================================================

    int fd() const override { return fd_; }

    // ============================================================
    // 设备信息 (8) - IGpuDriver
    // ============================================================

    int get_device_info(struct gpu_device_info* out) override {
        if (!out) return -1;
        if (!is_open()) return -1;
        if (ioctl(fd_, GPU_IOCTL_GET_DEVICE_INFO, out) < 0) {
            std::cerr << "GpuDriverClient: GPU_IOCTL_GET_DEVICE_INFO failed"
                      << " (errno=" << errno << ")\n";
            return -1;
        }
        return 0;
    }

    uint32_t get_warp_size() override {
        struct gpu_device_info info{};
        if (get_device_info(&info) == 0) return info.warp_size;
        return 0;
    }

    uint32_t get_simd_count() override {
        struct gpu_device_info info{};
        if (get_device_info(&info) == 0) return info.simd_count;
        return 0;
    }

    uint32_t get_peak_fp32_gflops() override {
        struct gpu_device_info info{};
        if (get_device_info(&info) == 0) return info.peak_fp32_gflops;
        return 0;
    }

    uint32_t get_max_clock_frequency() override {
        struct gpu_device_info info{};
        if (get_device_info(&info) == 0) return info.max_clock_frequency;
        return 0;
    }

    int get_driver_version_string(char* out, size_t size) override {
        if (!out || size == 0) return -1;
        struct gpu_device_info info{};
        if (get_device_info(&info) != 0) return -1;
        // 版本格式: 主.次.修订 (0x000500 = v0.5.0)
        u8 major = (info.driver_version >> 8) & 0xFF;
        u8 minor = (info.driver_version >> 4) & 0xFF;
        u8 patch = info.driver_version & 0xFF;
        snprintf(out, size, "v%u.%u.%u", major, minor, patch);
        return 0;
    }

    int get_marketing_name(char* out, size_t size) override {
        if (!out || size == 0) return -1;
        struct gpu_device_info info{};
        if (get_device_info(&info) != 0) return -1;
        strncpy(out, info.marketing_name, size - 1);
        out[size - 1] = '\0';
        return 0;
    }

    void print_device_info(std::ostream& os = std::cout) override {
        struct gpu_device_info info{};
        if (get_device_info(&info) != 0) {
            os << "Failed to get device info\n";
            return;
        }

        char version[32];
        char name[64];
        get_driver_version_string(version, sizeof(version));
        get_marketing_name(name, sizeof(name));

        os << "=== GPU Device Info ===\n";
        os << "  Vendor ID:     0x" << std::hex << info.vendor_id << std::dec << "\n";
        os << "  Device ID:     0x" << std::hex << info.device_id << std::dec << "\n";
        os << "  VRAM Size:     " << (info.vram_size / (1024*1024*1024)) << " GB\n";
        os << "  BAR0 Size:     " << (info.bar0_size / (1024*1024)) << " MB\n";
        os << "  Compute Units: " << info.compute_units << "\n";
        os << "  Warp Size:     " << info.warp_size << "\n";
        os << "  SIMD Count:    " << info.simd_count << "\n";
        os << "  Driver Ver:    " << version << "\n";
        os << "  Firmware Ver:   0x" << std::hex << info.firmware_version << std::dec << "\n";
        os << "  Max Clock:     " << info.max_clock_frequency << " MHz\n";
        os << "  Mem Clock:     " << info.max_memory_clock_frequency << " MHz\n";
        os << "  Mem Bus Width: " << info.memory_bus_width << "-bit\n";
        os << "  Peak FP32:     " << info.peak_fp32_gflops << " GFLOPS\n";
        os << "  PCIe BW:       " << info.pcie_bandwidth << " Mbps\n";
        os << "  Architecture:  0x" << std::hex << info.architecture_id << std::dec << "\n";
        os << "  Marketing:     " << name << "\n";
    }

    // ============================================================
    // 缓冲区对象 (4) - IGpuDriver (D6/D7/D8 对齐)
    // ============================================================

    /**
     * 分配 GPU 缓冲区对象 (D6)
     * @param size 分配大小（字节）
     * @param flags 分配标志 + domain 折入 flags (GPU_BO_DEVICE_LOCAL | GPU_MEM_DOMAIN_VRAM 等)
     * @return bo_handle (>=1) 成功，0 失败
     */
    uint64_t alloc_bo(uint64_t size, uint32_t flags) override {
        if (!is_open()) return 0;
        if (size == 0) return 0;

        struct gpu_alloc_bo_args args = {};
        args.size = size;
        args.domain = 0;       // domain 折入 flags (IGpuDriver 简化 API)
        args.flags = flags;

        if (ioctl(fd_, GPU_IOCTL_ALLOC_BO, &args) < 0) {
            std::cerr << "GpuDriverClient: GPU_IOCTL_ALLOC_BO failed"
                      << " (errno=" << errno << ")\n";
            return 0;
        }
        return static_cast<uint64_t>(args.handle);
    }

    /**
     * 分配 VRAM + DEVICE_LOCAL 缓冲区对象 (D6 一致)
     * @param size 分配大小（字节）
     * @param flags 额外 flags（VRAM + DEVICE_LOCAL 强制开启）
     * @return bo_handle (>=1) 成功，0 失败
     */
    uint64_t alloc_bo_vram(uint64_t size, uint32_t flags) override {
        return alloc_bo(size, GPU_MEM_DOMAIN_VRAM | GPU_BO_DEVICE_LOCAL | flags);
    }

    /**
     * 释放 GPU 缓冲区对象 (D7: u32 → u64 拓宽)
     * @param bo_handle 要释放的 BO handle
     * @return 0 成功，-1 失败
     */
    int free_bo(uint64_t bo_handle) override {
        if (!is_open()) return -1;
        if (bo_handle == 0) {
            std::cerr << "GpuDriverClient: free_bo called with bo_handle=0\n";
            return -1;
        }

        // ioctl 数据仍是 u32 (gpu_ioctl.h 未升级)
        uint32_t handle32 = static_cast<uint32_t>(bo_handle);
        if (ioctl(fd_, GPU_IOCTL_FREE_BO, &handle32) < 0) {
            std::cerr << "GpuDriverClient: GPU_IOCTL_FREE_BO failed"
                      << " (errno=" << errno << ")\n";
            return -1;
        }
        return 0;
    }

    /**
     * 映射 GPU 缓冲区对象 (D8: 返回 CPU 指针 + 移除 flags)
     * @param bo_handle BO handle
     * @param size 映射大小（字节）
     * @return CPU 虚拟地址，失败返回 nullptr
     */
    void* map_bo(uint64_t bo_handle, uint64_t size) override {
        if (!is_open()) return nullptr;
        if (bo_handle == 0 || size == 0) return nullptr;

        struct gpu_map_bo_args args = {};
        args.handle = static_cast<uint32_t>(bo_handle);
        args.flags = 0;

        if (ioctl(fd_, GPU_IOCTL_MAP_BO, &args) < 0) {
            std::cerr << "GpuDriverClient: GPU_IOCTL_MAP_BO failed"
                      << " (errno=" << errno << ")\n";
            return nullptr;
        }
        // D8: 返回 CPU 虚拟地址 (gpu_va 在 System C 中是统一虚拟地址空间)
        return reinterpret_cast<void*>(static_cast<uintptr_t>(args.gpu_va));
    }

    // ============================================================
    // 提交 (3, 返回 fence_id) - IGpuDriver
    // ============================================================

    int64_t submit_batch(uint32_t stream_id, const struct gpu_gpfifo_entry* entries,
                         uint32_t count, uint32_t flags) override {
        if (!is_open()) return -1;
        if (!entries || count == 0) return -1;

        struct gpu_pushbuffer_args args = {};
        args.stream_id = stream_id;
        args.entries_addr = reinterpret_cast<u64>(entries);
        args.count = count;
        args.flags = flags;
        args.fence_id = 0;  // 初始化
        args.va_space_handle = current_va_space_handle_;  // 透传 VA Space（0 = 跳过 H-1 校验）

        if (ioctl(fd_, GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH, &args) < 0) {
            std::cerr << "GpuDriverClient: GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH failed"
                      << " (errno=" << errno << ")\n";
            return -1;
        }
        return static_cast<int64_t>(args.fence_id);
    }

    int64_t submit_memcpy(uint32_t stream_id, uint64_t src_addr, uint64_t dst_addr,
                          uint64_t size, bool is_h2d) override {
        if (size == 0) return -1;

        struct gpu_gpfifo_entry entry = {};
        entry.valid = 1;
        entry.priv = 0;
        entry.method = GPU_OP_MEMCPY;  // 0x102
        entry.subchannel = 0;

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

    int64_t submit_launch(uint32_t stream_id, uint32_t kernel_index,
                          uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
                          uint32_t block_x, uint32_t block_y,
                          uint32_t block_z) override {
        if (grid_x == 0 || grid_y == 0 || grid_z == 0) return -1;
        if (block_x == 0 || block_y == 0 || block_z == 0) return -1;

        struct gpu_gpfifo_entry entry = {};
        entry.valid = 1;
        entry.priv = 0;
        entry.method = GPU_OP_LAUNCH_KERNEL;  // 0x100
        entry.subchannel = 0;

        entry.payload[0] = kernel_index;
        entry.payload[1] = grid_x | (grid_y << 16) | (static_cast<uint64_t>(grid_z) << 24);
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
    // Fence 等待 (2 重载) - IGpuDriver
    // ============================================================

    int wait_fence(uint64_t fence_id, uint32_t timeout_ms, uint32_t* status_out) override {
        if (!is_open()) return -1;
        if (fence_id == 0) return -1;
        if (!status_out) return -1;

        struct gpu_wait_fence_args args = {};
        args.fence_id = fence_id;
        args.timeout_ms = timeout_ms;

        if (ioctl(fd_, GPU_IOCTL_WAIT_FENCE, &args) < 0) {
            std::cerr << "GpuDriverClient: GPU_IOCTL_WAIT_FENCE failed"
                      << " (errno=" << errno << ")\n";
            return -1;
        }

        *status_out = args.status;
        return 0;
    }

    int wait_fence(uint64_t fence_id) override {
        uint32_t status = 0;
        return wait_fence(fence_id, 0, &status);
    }

    // ============================================================
    // VA Space 透传 (H-1 snake_case 迁移) - IGpuDriver
    // ============================================================

    void set_current_va_space(uint64_t va_space_handle) override {
        current_va_space_handle_ = va_space_handle;
    }

    uint64_t get_current_va_space() const override {
        return current_va_space_handle_;
    }

    // ------------------------------------------------------------
    // Deprecated CamelCase alias (H-1 既有 API, 1 release 过渡)
    // 内部调用 snake_case 版本; 调用方零修改
    // ------------------------------------------------------------

    /**
     * @deprecated H-2.5: 请使用 set_current_va_space()
     */
    void setCurrentVASpace(uint64_t va_space_handle) {
        set_current_va_space(va_space_handle);
    }

    /**
     * @deprecated H-2.5: 请使用 get_current_va_space()
     */
    uint64_t getCurrentVASpace() const {
        return get_current_va_space();
    }

    // ============================================================
    // H-3 Phase 2 占位 (5) - 抛异常保持纯虚覆盖合约
    // ============================================================

    uint64_t create_va_space(uint32_t flags) override {
        (void)flags;
        throw std::runtime_error("H-2.5: create_va_space not implemented; see H-3");
    }

    int destroy_va_space(uint64_t va_space_handle) override {
        (void)va_space_handle;
        throw std::runtime_error("H-2.5: destroy_va_space not implemented; see H-3");
    }

    int register_gpu(uint64_t va_space_handle, uint32_t gpu_id, uint32_t flags) override {
        (void)va_space_handle;
        (void)gpu_id;
        (void)flags;
        throw std::runtime_error("H-2.5: register_gpu not implemented; see H-3");
    }

    uint64_t create_queue(uint64_t va_space_handle, uint32_t queue_type,
                          uint32_t priority, uint64_t ring_buffer_size) override {
        (void)va_space_handle;
        (void)queue_type;
        (void)priority;
        (void)ring_buffer_size;
        throw std::runtime_error("H-2.5: create_queue not implemented; see H-3");
    }

    int destroy_queue(uint64_t queue_handle) override {
        (void)queue_handle;
        throw std::runtime_error("H-2.5: destroy_queue not implemented; see H-3");
    }

private:
    int fd_;                      // 设备文件描述符
    std::string device_path_;      // 设备路径
    uint64_t current_va_space_handle_ = 0;  // 默认 0 = 走 H-1 sentinel 跳过校验
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
