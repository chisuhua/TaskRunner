/**
 * memory_manager.cpp - GPU 内存管理器实现
 */

#include "memory_manager.hpp"
#include <cstring>
#include <stdexcept>

namespace taskrunner {

MemoryManager::~MemoryManager() {
    // 清理所有分配
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& [ptr, mem] : allocations_) {
        if (mem.host_ptr && mem.type != DeviceMemory::MemoryType::DEVICE_LOCAL) {
            std::free(mem.host_ptr);
        }
    }
    allocations_.clear();
}

uint64_t MemoryManager::allocate_ptr() {
    return next_ptr_.fetch_add(0x1000, std::memory_order_relaxed);
}

DeviceMemory MemoryManager::allocate(size_t size, DeviceMemory::MemoryType type) {
    if (size == 0) {
        return DeviceMemory();
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    uint64_t ptr = allocate_ptr();
    
    DeviceMemory mem(ptr, size, type);
    
    // HOST_VISIBLE 或 MANAGED 类型需要分配主机指针
    if (type == DeviceMemory::MemoryType::HOST_VISIBLE || 
        type == DeviceMemory::MemoryType::MANAGED) {
        mem.host_ptr = std::malloc(size);
        if (!mem.host_ptr) {
            // 分配失败，回滚
            return DeviceMemory();
        }
        std::memset(mem.host_ptr, 0, size);
    }
    
    allocations_[ptr] = mem;
    return mem;
}

void MemoryManager::free(DeviceMemory mem) {
    if (!mem.is_valid()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = allocations_.find(mem.device_ptr);
    if (it != allocations_.end()) {
        // 释放主机指针（如果有）
        if (it->second.host_ptr && 
            it->second.type != DeviceMemory::MemoryType::DEVICE_LOCAL) {
            std::free(it->second.host_ptr);
        }
        allocations_.erase(it);
    }
}

DeviceMemory MemoryManager::find(uint64_t device_ptr) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = allocations_.find(device_ptr);
    if (it != allocations_.end()) {
        return it->second;
    }
    
    return DeviceMemory();  // 返回无效的 DeviceMemory
}

void MemoryManager::memcpy_h2d(DeviceMemory dst, const void* src, size_t size) {
    if (!dst.is_valid() || !src) {
        throw std::invalid_argument("Invalid memory or source pointer");
    }
    
    if (size > dst.size) {
        throw std::out_of_range("Copy exceeds allocation bounds");
    }
    
    // Phase 1: 如果有主机指针，直接拷贝
    // Phase 2: 调用真实的 CUDA memcpy
    if (dst.host_ptr) {
        std::memcpy(dst.host_ptr, src, size);
    }
    // Stub 模式：仅记录操作，不实际拷贝
}

void MemoryManager::memcpy_d2h(void* dst, DeviceMemory src, size_t size) {
    if (!src.is_valid() || !dst) {
        throw std::invalid_argument("Invalid memory or destination pointer");
    }
    
    if (size > src.size) {
        throw std::out_of_range("Copy exceeds allocation bounds");
    }
    
    // Phase 1: 如果有主机指针，直接拷贝
    // Phase 2: 调用真实的 CUDA memcpy
    if (src.host_ptr) {
        std::memcpy(dst, src.host_ptr, size);
    } else {
        // Stub 模式：用 0 填充
        std::memset(dst, 0, size);
    }
}

void MemoryManager::memcpy_d2d(DeviceMemory dst, uint64_t dst_off,
                                DeviceMemory src, uint64_t src_off, size_t size) {
    (void)dst; (void)dst_off; (void)src; (void)src_off; (void)size;
    // Phase 1: Stub 实现
    // Phase 2: 实现真实的 D2D 拷贝逻辑
}

size_t MemoryManager::total_allocated() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t total = 0;
    for (const auto& [ptr, mem] : allocations_) {
        total += mem.size;
    }
    return total;
}

size_t MemoryManager::allocation_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return allocations_.size();
}

} // namespace taskrunner
