/**
 * mock_gpu_driver.hpp - MockGpuDriver 测试夹具 (H-2.5)
 *
 * 实现 IGpuDriver 28 个方法，每个方法：
 * 1. record(method, args) 记录调用
 * 2. if injected_error 触发 -> 返回 sentinel
 * 3. 否则返回 canned value（递增 handle 或固定值）
 *
 * 提供:
 *  - history() / clear_history() 访问录制
 *  - inject_error(method, bool) 错误注入
 *  - set_canned_return(method, value) 精确控制返回值
 *  - call_count(method) 便捷查询
 *
 * header-only, 单测 include 即用
 */

#ifndef TASKRUNNER_MOCK_GPU_DRIVER_HPP
#define TASKRUNNER_MOCK_GPU_DRIVER_HPP

#include "shared/igpu_driver.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace async_task {
namespace gpu {

/**
 * MockGpuDriver - IGpuDriver mock 实现
 */
class MockGpuDriver : public IGpuDriver {
public:
    struct CallRecord {
        std::string method;
        std::vector<uint64_t> args_u64;
        std::vector<uint32_t> args_u32;
        bool injected_error{false};
    };

    MockGpuDriver() = default;
    ~MockGpuDriver() override = default;

    MockGpuDriver(const MockGpuDriver&) = delete;
    MockGpuDriver& operator=(const MockGpuDriver&) = delete;

    // ============================================================
    // 核心生命周期 (3)
    // ============================================================
    int open() override {
        record("open");
        if (injected_errors_["open"]) return -1;
        return canned_int("open", 0);
    }

    int close() override {
        record("close");
        if (injected_errors_["close"]) return -1;
        return canned_int("close", 0);
    }

    bool is_open() const override {
        record("is_open");
        if (injected_errors_["is_open"]) return false;
        return true;
    }

    // ============================================================
    // FD 访问 (1)
    // ============================================================
    int fd() const override {
        record("fd");
        if (injected_errors_["fd"]) return -2;
        return canned_int("fd", 999);
    }

    // ============================================================
    // 设备信息 (8)
    // ============================================================
    int get_device_info(struct gpu_device_info* out) override {
        record("get_device_info");
        if (!out) return -1;
        if (injected_errors_["get_device_info"]) return -1;
        std::memset(out, 0, sizeof(*out));
        out->vendor_id = 0x10DE;
        out->device_id = 0xCAFE;
        out->warp_size = 32;
        out->simd_count = 64;
        out->driver_version = 0x000900;
        std::strncpy(out->marketing_name, "Mock GPU", sizeof(out->marketing_name) - 1);
        return canned_int("get_device_info", 0);
    }

    uint32_t get_warp_size() override {
        record("get_warp_size");
        if (injected_errors_["get_warp_size"]) return 0;
        return canned_u32("get_warp_size", 32);
    }

    uint32_t get_simd_count() override {
        record("get_simd_count");
        if (injected_errors_["get_simd_count"]) return 0;
        return canned_u32("get_simd_count", 64);
    }

    uint32_t get_peak_fp32_gflops() override {
        record("get_peak_fp32_gflops");
        if (injected_errors_["get_peak_fp32_gflops"]) return 0;
        return canned_u32("get_peak_fp32_gflops", 12345);
    }

    uint32_t get_max_clock_frequency() override {
        record("get_max_clock_frequency");
        if (injected_errors_["get_max_clock_frequency"]) return 0;
        return canned_u32("get_max_clock_frequency", 2000);
    }

    int get_driver_version_string(char* out, size_t size) override {
        record("get_driver_version_string");
        if (!out || size == 0) return -1;
        if (injected_errors_["get_driver_version_string"]) return -1;
        std::strncpy(out, "v0.9.0-mock", size - 1);
        out[size - 1] = '\0';
        return 0;
    }

    int get_marketing_name(char* out, size_t size) override {
        record("get_marketing_name");
        if (!out || size == 0) return -1;
        if (injected_errors_["get_marketing_name"]) return -1;
        std::strncpy(out, "Mock GPU", size - 1);
        out[size - 1] = '\0';
        return 0;
    }

    void print_device_info(std::ostream& os = std::cout) override {
        (void)os;
        record("print_device_info");
    }

    // ============================================================
    // 缓冲区对象 (4) - 递增 handle / host malloc
    // ============================================================
    uint64_t alloc_bo(uint64_t size, uint32_t flags) override {
        record_alloc_bo("alloc_bo", size, flags);
        if (injected_errors_["alloc_bo"]) return 0;
        if (size == 0) return 0;
        uint64_t override_val = canned_u64("alloc_bo", 0);
        if (override_val != 0) return override_val;
        return next_handle_.fetch_add(1, std::memory_order_relaxed);
    }

    uint64_t alloc_bo_vram(uint64_t size, uint32_t flags) override {
        record_alloc_bo("alloc_bo_vram", size, flags);
        if (injected_errors_["alloc_bo_vram"]) return 0;
        if (size == 0) return 0;
        uint64_t override_val = canned_u64("alloc_bo_vram", 0);
        if (override_val != 0) return override_val;
        return next_handle_.fetch_add(1, std::memory_order_relaxed);
    }

    int free_bo(uint64_t bo_handle) override {
        record("free_bo", {bo_handle});
        if (injected_errors_["free_bo"]) return -1;
        if (bo_handle == 0) return -1;
        return canned_int("free_bo", 0);
    }

    void* map_bo(uint64_t bo_handle, uint64_t size) override {
        record("map_bo", {bo_handle, size});
        if (injected_errors_["map_bo"]) return nullptr;
        if (bo_handle == 0 || size == 0) return nullptr;
        return std::malloc(size);
    }

    // ============================================================
    // 提交 (3) - 返回递增 fence_id
    // ============================================================
    int64_t submit_batch(uint32_t stream_id,
                         const struct gpu_gpfifo_entry* entries,
                         uint32_t count, uint32_t flags) override {
        record("submit_batch", {stream_id, count, flags});
        (void)entries;
        if (injected_errors_["submit_batch"]) return -1;
        return static_cast<int64_t>(next_fence_id_.fetch_add(1, std::memory_order_relaxed));
    }

    int64_t submit_memcpy(uint32_t stream_id, uint64_t src_addr, uint64_t dst_addr,
                          uint64_t size, bool is_h2d) override {
        record("submit_memcpy", {stream_id, src_addr, dst_addr, size, is_h2d ? 1u : 0u});
        if (injected_errors_["submit_memcpy"]) return -1;
        return static_cast<int64_t>(next_fence_id_.fetch_add(1, std::memory_order_relaxed));
    }

    int64_t submit_launch(uint32_t stream_id, uint32_t kernel_index,
                          uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
                          uint32_t block_x, uint32_t block_y,
                          uint32_t block_z) override {
        record("submit_launch", {stream_id, kernel_index,
                                 grid_x, grid_y, grid_z,
                                 block_x, block_y, block_z});
        if (injected_errors_["submit_launch"]) return -1;
        return static_cast<int64_t>(next_fence_id_.fetch_add(1, std::memory_order_relaxed));
    }

    // ============================================================
    // Fence 等待 (2 重载)
    // ============================================================
    int wait_fence(uint64_t fence_id, uint32_t timeout_ms, uint32_t* status_out) override {
        record("wait_fence", {fence_id, timeout_ms});
        if (injected_errors_["wait_fence"]) return -1;
        if (status_out) *status_out = 1;
        return canned_int("wait_fence", 0);
    }

    int wait_fence(uint64_t fence_id) override {
        record("wait_fence_1arg", {fence_id});
        if (injected_errors_["wait_fence"]) return -1;
        return 0;
    }

    // ============================================================
    // VA Space 透传 (2)
    // ============================================================
    void set_current_va_space(uint64_t va_space_handle) override {
        record("set_current_va_space", {va_space_handle});
        current_va_space_handle_ = va_space_handle;
    }

    uint64_t get_current_va_space() const override {
        record("get_current_va_space");
        return current_va_space_handle_;
    }

    // ============================================================
    // H-3 Phase 2 占位 (5)
    // ============================================================
    uint64_t create_va_space(uint32_t flags) override {
        record("create_va_space", {flags});
        if (injected_errors_["create_va_space"]) return 0;
        return canned_u64("create_va_space", 0);
    }

    int destroy_va_space(uint64_t va_space_handle) override {
        record("destroy_va_space", {va_space_handle});
        if (injected_errors_["destroy_va_space"]) return -1;
        return 0;
    }

    int register_gpu(uint64_t va_space_handle, uint32_t gpu_id, uint32_t flags) override {
        record("register_gpu", {va_space_handle, gpu_id, flags});
        if (injected_errors_["register_gpu"]) return -1;
        return 0;
    }

    uint64_t create_queue(uint64_t va_space_handle, uint32_t queue_type,
                          uint32_t priority, uint64_t ring_buffer_size) override {
        record("create_queue", {va_space_handle, queue_type, priority, ring_buffer_size});
        if (injected_errors_["create_queue"]) return 0;
        return canned_u64("create_queue", 0);
    }

    int destroy_queue(uint64_t queue_handle) override {
        record("destroy_queue", {queue_handle});
        if (injected_errors_["destroy_queue"]) return -1;
        return 0;
    }

    // ============================================================
    // Test API
    // ============================================================
    const std::vector<CallRecord>& history() const { return history_; }
    void clear_history() { history_.clear(); }

    void inject_error(const std::string& method, bool enable) {
        injected_errors_[method] = enable;
    }

    void set_canned_return(const std::string& method, uint64_t value) {
        canned_u64_[method] = value;
    }

    size_t call_count(const std::string& method) const {
        size_t n = 0;
        for (const auto& r : history_) {
            if (r.method == method) ++n;
        }
        return n;
    }

    size_t total_calls() const { return history_.size(); }

private:
    void record(const std::string& method,
                const std::vector<uint64_t>& args = {}) const {
        CallRecord r;
        r.method = method;
        r.args_u64 = args;
        r.injected_error = false;
        history_.push_back(std::move(r));
    }

    void record_alloc_bo(const std::string& method, uint64_t size, uint32_t flags) {
        CallRecord r;
        r.method = method;
        r.args_u64 = {size};
        r.args_u32 = {flags};
        r.injected_error = false;
        history_.push_back(std::move(r));
    }

    int canned_int(const std::string& method, int default_val) const {
        auto it = canned_u64_.find(method);
        if (it != canned_u64_.end()) {
            return static_cast<int>(it->second);
        }
        return default_val;
    }

    uint32_t canned_u32(const std::string& method, uint32_t default_val) const {
        auto it = canned_u64_.find(method);
        if (it != canned_u64_.end()) {
            return static_cast<uint32_t>(it->second);
        }
        return default_val;
    }

    uint64_t canned_u64(const std::string& method, uint64_t default_val) const {
        auto it = canned_u64_.find(method);
        if (it != canned_u64_.end()) {
            return it->second;
        }
        return default_val;
    }

    mutable std::vector<CallRecord> history_;
    mutable std::unordered_map<std::string, bool> injected_errors_;
    mutable std::unordered_map<std::string, uint64_t> canned_u64_;
    mutable std::atomic<uint64_t> next_handle_{1};
    mutable std::atomic<uint64_t> next_fence_id_{1};
    uint64_t current_va_space_handle_{0};
};

}  // namespace gpu
}  // namespace async_task

#endif  // TASKRUNNER_MOCK_GPU_DRIVER_HPP
