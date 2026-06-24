// SCOPE: SHARED
/**
 * memory_manager.hpp - GPU 内存管理器
 * 
 * DDS v1.2 架构定义：
 * - 独立管理 GPU 内存资源追踪
 * - 支持 HOST_VISIBLE, DEVICE_LOCAL, MANAGED 三种类型
 * - 提供底层内存拷贝原语
 */

#ifndef TASKRUNNER_MEMORY_MANAGER_HPP
#define TASKRUNNER_MEMORY_MANAGER_HPP

#include <map>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <cstddef>

namespace taskrunner {

/**
 * DeviceMemory - GPU 内存描述符
 */
struct DeviceMemory {
    uint64_t device_ptr{0};
    size_t size{0};
    
    enum class MemoryType {
        HOST_VISIBLE,    // 主机可访问（ pinned memory）
        DEVICE_LOCAL,    // 仅设备可访问
        MANAGED          // 统一内存（managed memory）
    };
    
    MemoryType type{MemoryType::DEVICE_LOCAL};
    void* host_ptr{nullptr};  // HOST_VISIBLE/MANAGED 时有效
    
    DeviceMemory() = default;
    
    DeviceMemory(uint64_t ptr, size_t sz, MemoryType t)
        : device_ptr(ptr), size(sz), type(t) {}
    
    bool is_valid() const { return device_ptr != 0; }
};

/**
 * MemoryManager - GPU 内存管理器
 * 
 * 职责：
 * 1. 分配/释放 GPU 内存
 * 2. 追踪内存分配状态
 * 3. 提供内存拷贝原语（H2D, D2H, D2D）
 */
class MemoryManager {
public:
    MemoryManager() = default;
    ~MemoryManager();
    
    // 禁止拷贝
    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;
    
    // 允许移动
    MemoryManager(MemoryManager&&) = default;
    MemoryManager& operator=(MemoryManager&&) = default;
    
    // ========== 内存分配 ==========
    
    /**
     * 分配 GPU 内存
     * @param size 分配大小（字节）
     * @param type 内存类型
     * @return DeviceMemory 描述符
     */
    DeviceMemory allocate(size_t size, 
                          DeviceMemory::MemoryType type = DeviceMemory::MemoryType::DEVICE_LOCAL);
    
    /**
     * 释放 GPU 内存
     * @param mem 内存描述符
     */
    void free(DeviceMemory mem);
    
    /**
     * 通过地址查找内存描述符
     * @param device_ptr 设备地址
     * @return DeviceMemory 描述符，不存在返回无效的 DeviceMemory
     */
    DeviceMemory find(uint64_t device_ptr);
    
    // ========== 内存拷贝原语 ==========
    
    /**
     * Host to Device 拷贝
     * @param dst 目标设备内存
     * @param src 源主机指针
     * @param size 拷贝大小
     */
    void memcpy_h2d(DeviceMemory dst, const void* src, size_t size);
    
    /**
     * Device to Host 拷贝
     * @param dst 目标主机指针
     * @param src 源设备内存
     * @param size 拷贝大小
     */
    void memcpy_d2h(void* dst, DeviceMemory src, size_t size);
    
    /**
     * Device to Device 拷贝（支持偏移）
     * @param dst 目标设备内存
     * @param dst_off 目标偏移
     * @param src 源设备内存
     * @param src_off 源偏移
     * @param size 拷贝大小
     */
    void memcpy_d2d(DeviceMemory dst, uint64_t dst_off,
                    DeviceMemory src, uint64_t src_off, size_t size);
    
    // ========== 统计信息 ==========
    
    /**
     * 获取已分配内存总量
     * @return 总字节数
     */
    size_t total_allocated() const;
    
    /**
     * 获取分配数量
     * @return 分配次数
     */
    size_t allocation_count() const;
    
private:
    std::map<uint64_t, DeviceMemory> allocations_;
    std::atomic<uint64_t> next_ptr_{0x10000};  // 从 0x10000 开始分配
    mutable std::mutex mutex_;
    
    uint64_t allocate_ptr();
};

} // namespace taskrunner

#endif // TASKRUNNER_MEMORY_MANAGER_HPP
