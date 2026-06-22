# Design: h2-5-architecture-foundation

> **依赖**: `proposal.md` 已完成
> **作用**: 解释 HOW 构建 IGpuDriver 抽象 + 2 实现 + DI + Mock + CLI 修复
> **状态**: ⚠️ DRAFT — D6-D11 已 FINALIZED，待 review 后激活

## Context

### H-2.5 前置状态（2026-06-22 起草基线）

| 任务组 | 状态 | 来源 |
|--------|------|------|
| `IGpuDriver` 抽象接口 | ✅ **已起草** `include/igpu_driver.hpp`（311 行 28 个纯虚方法）| 本 change 起草基线 |
| `GpuDriverClient` 实现 `IGpuDriver` | ❌ 未做 | 本 change 范围 |
| `CudaStub` 命名空间迁移 + 实现 `IGpuDriver` | ❌ 未做（仍在 `taskrunner` 命名空间）| 本 change 范围 |
| `MockGpuDriver` 测试夹具 | ❌ 未做 | 本 change 范围 |
| `CudaScheduler` DI 重构 | ❌ 未做（仍持 `CudaStub*`）| 本 change 范围 |
| CLI 死调用修复 | ❌ 未做（`init_gpu_client()` 从未被调用）| 本 change 范围 |
| H-3（5 个 Phase 2 ioctl wrapper）| ⏳ 待 H-2.5 完成后开始 | 后续 change |

### `IGpuDriver` 抽象接口形状（已起草，引用 `include/igpu_driver.hpp`）

28 个纯虚方法，分 6 类：

| 类 | 方法数 | 范围 | 已实现 |
|---|--------|------|--------|
| 核心生命周期 | 3 | `open()` / `close()` / `is_open()` | GpuDriverClient (line 55-67) |
| FD 访问 | 1 | `fd()` | GpuDriverClient (line 77) |
| 设备信息 | 8 | `get_device_info` 等 | GpuDriverClient (line 88-134) |
| 缓冲区对象 | 4 | `alloc_bo` / `alloc_bo_vram` / `free_bo` / `map_bo` | **需 D6/D7/D8 对齐**（line 146-169）|
| 提交（返回 fence_id）| 3 | `submit_batch` / `submit_memcpy` / `submit_launch` | GpuDriverClient (line 183-214) |
| Fence 等待（2 重载）| 2 | `wait_fence` | GpuDriverClient (line 227-235) |
| VA Space 透传 | 2 | `set_current_va_space` / `get_current_va_space` | GpuDriverClient (line 250-256)（H-1 snake_case 迁移）|
| H-3 Phase 2 占位 | 5 | `create_va_space` / `destroy_va_space` / `register_gpu` / `create_queue` / `destroy_queue` | 本 change 抛 `std::runtime_error("not implemented")`（H-3 范围）|

### 既有 `GpuDriverClient` 签名（与 IGpuDriver 不匹配部分）

| 方法 | 当前 `GpuDriverClient` 签名 | `IGpuDriver` 签名 | 决策 |
|------|---------------------------|------------------|------|
| `alloc_bo` | `(uint64_t size, uint32_t domain, uint32_t flags, uint32_t* handle, uint64_t* gpu_va) → int`（5 参数）| `(uint64_t size, uint32_t flags) → uint64_t`（2 参数）| **D6** |
| `alloc_bo_vram` | `(uint64_t size, uint32_t* handle, uint64_t* gpu_va) → int`（3 参数）| `(uint64_t size, uint32_t flags) → uint64_t`（2 参数）| 跟 D6 改写 |
| `free_bo` | `(uint32_t handle) → int`（u32 handle）| `(uint64_t bo_handle) → int`（u64 handle）| **D7** |
| `map_bo` | `(uint32_t handle, uint32_t flags, uint64_t* gpu_va) → int`（3 参数，输出 gpu_va）| `(uint64_t bo_handle, uint64_t size) → void*`（2 参数，返回 CPU 指针）| **D8** |
| `setCurrentVASpace` / `getCurrentVASpace` | CamelCase H-1 既有 | snake_case `set_current_va_space` / `get_current_va_space` | 加 snake_case alias 覆盖，CamelCase 保留作 deprecated |

### H-3 后续接口（已起草在 IGpuDriver 中，本 change 仅占位）

```cpp
// include/igpu_driver.hpp line 267-302
virtual uint64_t create_va_space(uint32_t flags) = 0;   // H-3 实现
virtual int destroy_va_space(uint64_t va_space_handle) = 0;  // H-3 实现
virtual int register_gpu(uint64_t va_space_handle, uint32_t gpu_id, uint32_t flags) = 0;  // H-3 实现
virtual uint64_t create_queue(uint64_t va_space_handle, uint32_t queue_type,
                              uint32_t priority, uint64_t ring_buffer_size) = 0;  // H-3 实现
virtual int destroy_queue(uint64_t queue_handle) = 0;  // H-3 实现
```

本 change 中 `GpuDriverClient` 与 `CudaStub` 这 5 个方法**抛 `std::runtime_error("not implemented; see H-3")`**（保持纯虚被覆盖的合约，不允许空实现）。

## Goals / Non-Goals

### Goals

- `GpuDriverClient` 实现 `IGpuDriver` 全部 28 方法（其中 5 个 Phase 2 占位抛异常）
- `CudaStub` 迁移到 `async_task::gpu` 命名空间 + 实现 `IGpuDriver` 全部 28 方法
- `CudaScheduler` 接受 `IGpuDriver*` 注入 + 向后兼容（auto-create fallback）
- `MockGpuDriver` 测试夹具支持单测注入
- CLI 入口 `init_gpu_client()` / `shutdown_gpu_client()` 真正被调用
- 既有 8 个 `test_cuda_scheduler` 用例继续 pass（无回归）
- 4 个 BO 方法按 D6/D7/D8 对齐（破坏性变更）
- H-1 既有 `setCurrentVASpace()` / `getCurrentVASpace()` 保留作 deprecated alias
- 跨仓同步遵循 H-1 closeout pattern（D1/D3/D5）

### Non-Goals

- **不**实现 5 个 Phase 2 ioctl wrapper（属于 H-3 范围）
- **不**改 `gpu_ioctl.h` / `gpu_types.h`（上游 SSOT，TaskRunner 通过符号链接消费）
- **不**实现多 GPU / peer-to-peer 场景（Phase 3+）
- **不**实现 mmap 快速路径（`MAP_QUEUE_RING`）
- **不**改 UsrLinuxEmu 侧任何代码
- **不**重命名 H-1 既有 CamelCase 方法（保留 1 release 过渡）

## Decisions（D6-D11 FINALIZED）

### D6 — `alloc_bo` 签名 = `(size, flags) → u64`

**决策**：`GpuDriverClient::alloc_bo(uint64_t size, uint32_t flags) → uint64_t`（2 参数，返回 bo_handle）。

**理由**：
- **IGpuDriver 已起草的形状**（`include/igpu_driver.hpp:146`）明确 `(size, flags) → u64`，对齐接口
- **`domain` 折入 `flags`**：`gpu_ioctl.h` 中 `domain` 与 `flags` 都是 `u32` 位域，可以复用同一位空间（`GPU_MEM_DOMAIN_VRAM = 0x1` / `GPU_BO_DEVICE_LOCAL = 0x2` 等已是独立位掩码）；单一 `flags` 参数简化 API
- **`gpu_va` 移到 `map_bo()`**：`alloc_bo` 不应负责映射；`map_bo` 单独负责 CPU 映射并返回指针（D8）
- **错误表达**：返回 `0` = 失败（与 `create_va_space` 等 handle 类方法一致）；调用方检查 `if (handle == 0) error`

**取舍**：
- 当前 5 参数（含两个 out 参数）信息量大但繁琐；新 2 参数简洁但需 2 次调用（alloc + map）
- 对于不需 CPU 访问的 BO（如纯 device buffer），可跳过 `map_bo()` 调用
- 既有 `alloc_bo_vram` 改写为 `alloc_bo(size, GPU_MEM_DOMAIN_VRAM | GPU_BO_DEVICE_LOCAL)` 一行调用

**备选 A（保留 5 参数原样）拒绝理由**：与 IGpuDriver 接口形状不一致；接口统一性是 H-2.5 的核心目标。

### D7 — `free_bo` handle 类型 = `uint64_t`

**决策**：`GpuDriverClient::free_bo(uint64_t bo_handle) → int`。

**理由**：
- **与 H-3 一致**：`create_va_space` / `create_queue` 返回 `uint64_t`；`free_bo` 接收 `uint64_t` 保持 handle 类型统一
- **未来扩展**：u64 handle 容纳 `gpu_ioctl.h` 后续可能的扩展字段（如 generation counter、sub-id）
- **零成本拓宽**：当前 `gpu_ioctl.h::gpu_bo_handle_t` 是 u32，拓宽仅客户端类型调整，ioctl 数据不变

**取舍**：
- 短期：现有调用方需 `static_cast<uint64_t>(u32_handle)` —— 编译器隐式转换无需显式
- 长期：未来若 `gpu_ioctl.h` 升级到 u64 handle，客户端零修改

**备选 A（保留 u32）拒绝理由**：与 H-3 的 `uint64_t` 习惯不一致；频繁 `u32`↔`u64` 转换增加代码噪声。

### D8 — `map_bo` 签名 = `(handle, size) → void*`

**决策**：`GpuDriverClient::map_bo(uint64_t bo_handle, uint64_t size) → void*`（2 参数，返回 CPU 虚拟地址；失败返回 `nullptr`）。

**理由**：
- **返回类型统一**：CPU 指针是 `map_bo` 的唯一产出，用返回值表达比 out 参数更清晰
- **`flags` 移除**：`gpu_ioctl.h::gpu_map_bo_args::flags` 当前仅有 `GPU_MAP_READ/WRITE` 等，与 BO 自身 `flags` 重复；简化 API
- **C++ 习惯用法**：返回 `T*` 或 `nullptr` 比 `int + out param` 更地道（`std::filesystem::file::open` / `mmap` 都是这个模式）
- **`size` 必填**：调用方明确知道映射多大，避免 `gpu_ioctl.h` 默认值歧义

**取舍**：
- 返回 `nullptr` 的"双重含义"（失败 vs 合法零地址）—— Linux `mmap` 也是这模式，约定俗成
- `size == 0` 守卫：`map_bo` 拒绝，返回 `nullptr`（无 ioctl）

**备选 A（保留 `int + *gpu_va`）拒绝理由**：out 参数 + 返回码的模式容易忘记检查返回值；与 IGpuDriver 接口形状不一致。

### D9 — `CudaStub` 命名空间迁移 = `taskrunner` → `async_task::gpu`

**决策**：
1. `CudaStub` 类定义迁移到 `namespace async_task::gpu`（与 `IGpuDriver` 同命名空间）
2. 旧 `namespace taskrunner` 提供 `using CudaStub = async_task::gpu::CudaStub;` 作为 **1 release 的过渡**
3. 旧 `LaunchParams` / `CudaResult` 等类型同步迁移到 `async_task::gpu`
4. **不**用 adapter 模式（在 `async_task::gpu` 包一层 `CudaStubAdapter` 转发到原 `taskrunner::CudaStub`）

**理由**：
- **接口归属一致**：`IGpuDriver` 在 `async_task::gpu`，两个实现也应在此命名空间，读者一眼就能找到所有相关类型
- **避免运行时 indirection**：adapter 模式多一层虚函数调用；直接迁移更高效（CudaStub 调用频率高）
- **测试一致**：MockGpuDriver 也在 `async_task::gpu`，三者并列
- **零 ABI break**：`using` alias 保证旧调用方零修改

**取舍**：
- 1 release 的 alias 是临时债务（task §7 显式记录在 deprecated list）
- 跨 TU 的 `#include "cuda_stub.hpp"` 仍能找到符号（命名空间外层是项目根）

**备选 A（adapter 模式）拒绝理由**：
- 多一层间接调用（性能成本）
- 维护两套类（复杂度↑）
- 与 D9 决策"统一命名空间"目标违背

### D10 — `CudaScheduler` DI = `(IGpuDriver* = nullptr)` + auto-create

**决策**：
```cpp
namespace taskrunner {

class CudaScheduler {
public:
    explicit CudaScheduler(async_task::gpu::IGpuDriver* driver = nullptr);
    // ...
private:
    async_task::gpu::IGpuDriver* driver_{nullptr};
    bool owns_driver_{false};  // true if auto-created via new CudaStub()
};

}
```

构造逻辑：
- 若 `driver != nullptr` → `driver_ = driver; owns_driver_ = false`
- 若 `driver == nullptr` → `driver_ = new CudaStub(); owns_driver_ = true`（向后兼容）
- 析构时若 `owns_driver_` → `delete driver_`

**理由**：
- **DI 解耦**：`CudaScheduler` 不知道具体是 `GpuDriverClient` / `CudaStub` / `MockGpuDriver`
- **向后兼容**：现有 8 个 `test_cuda_scheduler` 用例（`new CudaScheduler()` 无参）零修改
- **测试友好**：`new CudaScheduler(&mock)` 直接注入 MockGpuDriver
- **`CudaStub* → IGpuDriver*` 隐式转换**：CudaStub 实现 IGpuDriver，旧 `CudaScheduler(CudaStub*)` 调用方也兼容

**取舍**：
- `owns_driver_` 是 RAII 复杂度（管理非自身 new 的指针）—— 但比裸 `delete` 安全
- nullptr 时 auto-create 的语义稍隐式 —— 在文档与 R3 Requirement 显式说明

**备选 A（强制 `IGpuDriver*` 无 nullptr）拒绝理由**：破坏现有测试；要求所有 caller 显式注入是过度设计。

### D11 — CLI 死调用修复 = `init_gpu_client()` 启动 + `shutdown_gpu_client()` 退出

**决策**：
```cpp
// src/cli_main.cpp
#include "gpu_driver_client.h"  // 引入 init_gpu_client / shutdown_gpu_client

int main(int argc, char* argv[]) {
    // D11: 启动时调用 init_gpu_client()
    if (async_task::gpu::init_gpu_client() != 0) {
        std::cerr << "Failed to init GPU client (continuing in stub mode)\n";
        // 不返回 1：保留向后兼容（CLI 测试模式不需要真 GPU）
    }

    int ret = cmd_buffer_v2_main(argc, argv);

    // D11: 退出时调用 shutdown_gpu_client()
    async_task::gpu::shutdown_gpu_client();
    return ret;
}
```

**理由**：
- **死代码消除**：`init_gpu_client()` 与 `shutdown_gpu_client()` 已在 `gpu_driver_client.cpp` 定义（`extern` 声明在 `.h:508-513`），但**无调用方**
- **不强制失败**：测试模式（`--test`）不需要真 GPU；失败时打印警告 + 继续运行（保留 H-1 前的行为）
- **生产路径打通**：`cuda_alloc` / `cuda_memcpy` / `cuda_launch` / `cuda_wait` 4 个 CLI 命令（`src/cmd_cuda.cpp`）首次真正能调到 `g_gpu_client`

**取舍**：
- 失败时不强制退出 —— CLI 工具的常见模式（`grep --no-match` 不报错退出）
- `init_gpu_client()` 内部失败仍 `g_gpu_client = nullptr` —— 调用方需 guard（既有 `cmd_cuda.cpp` 代码应已有）

**备选 A（启动失败时强制返回 1）拒绝理由**：破坏 `--test` 模式（无 /dev/gpgpu0 设备时不能跑测试）。

## Implementation Overview

### `GpuDriverClient` 实现 `IGpuDriver`

```cpp
// include/gpu_driver_client.h
namespace async_task {
namespace gpu {

class GpuDriverClient : public IGpuDriver {
public:
    explicit GpuDriverClient(const char* device_path = "/dev/gpgpu0");
    ~GpuDriverClient() override;
    GpuDriverClient(const GpuDriverClient&) = delete;
    GpuDriverClient& operator=(const GpuDriverClient&) = delete;

    // 3 个核心生命周期
    int open() override;
    int close() override;
    bool is_open() const override;

    // 1 个 FD 访问
    int fd() const override;

    // 8 个设备信息
    int get_device_info(struct gpu_device_info* out) override;
    uint32_t get_warp_size() override;
    uint32_t get_simd_count() override;
    uint32_t get_peak_fp32_gflops() override;
    uint32_t get_max_clock_frequency() override;
    int get_driver_version_string(char* out, size_t size) override;
    int get_marketing_name(char* out, size_t size) override;
    void print_device_info(std::ostream& os = std::cout) override;

    // 4 个 BO（按 D6/D7/D8 对齐）
    uint64_t alloc_bo(uint64_t size, uint32_t flags) override;
    uint64_t alloc_bo_vram(uint64_t size, uint32_t flags) override;
    int free_bo(uint64_t bo_handle) override;
    void* map_bo(uint64_t bo_handle, uint64_t size) override;

    // 3 个提交（返回 fence_id）
    int64_t submit_batch(uint32_t stream_id, const struct gpu_gpfifo_entry* entries,
                         uint32_t count, uint32_t flags) override;
    int64_t submit_memcpy(uint32_t stream_id, uint64_t src_addr, uint64_t dst_addr,
                          uint64_t size, bool is_h2d) override;
    int64_t submit_launch(uint32_t stream_id, uint32_t kernel_index,
                          uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
                          uint32_t block_x, uint32_t block_y, uint32_t block_z) override;

    // 2 个 wait_fence 重载
    int wait_fence(uint64_t fence_id, uint32_t timeout_ms, uint32_t* status_out) override;
    int wait_fence(uint64_t fence_id) override;

    // 2 个 VA Space 透传（snake_case H-1 迁移 + CamelCase alias 保留）
    void set_current_va_space(uint64_t va_space_handle) override;
    uint64_t get_current_va_space() const override;
    // Deprecated alias (1 release 过渡)
    void setCurrentVASpace(uint64_t va_space_handle) { set_current_va_space(va_space_handle); }
    uint64_t getCurrentVASpace() const { return get_current_va_space(); }

    // 5 个 H-3 占位（抛异常保持合约）
    uint64_t create_va_space(uint32_t flags) override {
        throw std::runtime_error("create_va_space not implemented; see H-3");
    }
    // ... 其余 4 个同 ...

private:
    int fd_;
    std::string device_path_;
    uint64_t current_va_space_handle_ = 0;
};

// 全局 + 初始化
extern GpuDriverClient* g_gpu_client;
int init_gpu_client();
void shutdown_gpu_client();

}}  // namespace async_task::gpu
```

### `CudaStub` 命名空间迁移 + `IGpuDriver` 实现

```cpp
// include/cuda_stub.hpp
namespace async_task {
namespace gpu {

class CudaStub : public IGpuDriver {
public:
    CudaStub();
    ~CudaStub() override;
    CudaStub(const CudaStub&) = delete;
    CudaStub& operator=(const CudaStub&) = delete;

    // 既有 CUDA Driver API 方法保留（命名空间外层 client 代码可继续调用）
    CudaResult initialize();
    void shutdown();
    bool is_initialized() const { return initialized_; }
    CudaResult mem_alloc(size_t size, uint64_t* device_ptr);
    // ... 等既有方法 ...

    // 28 个 IGpuDriver 覆盖（mock 语义：递增 handle + 内存 mock + 零 ioctl）
    int open() override { return 0; }
    int close() override { return 0; }
    bool is_open() const override { return true; }
    int fd() const override { return -1; }  // mock 无 fd
    // ... 等 28 个 ...
    uint64_t alloc_bo(uint64_t size, uint32_t flags) override {
        return next_bo_handle_++;  // 递增
    }
    int free_bo(uint64_t bo_handle) override {
        return 0;  // mock 立即成功
    }
    void* map_bo(uint64_t bo_handle, uint64_t size) override {
        return malloc(size);  // mock 用 host malloc
    }
    int64_t submit_memcpy(uint32_t stream_id, uint64_t src, uint64_t dst,
                          uint64_t size, bool is_h2d) override {
        return next_fence_id_++;  // mock fence_id
    }
    // ... 其余方法 ...
};

}}

// 旧命名空间 alias（1 release 过渡）
namespace taskrunner {
    using CudaStub = async_task::gpu::CudaStub;
    using CudaResult = async_task::gpu::CudaResult;
    using LaunchParams = async_task::gpu::LaunchParams;
}
```

### `CudaScheduler` DI 重构

```cpp
// include/cuda_scheduler.hpp
namespace taskrunner {

class CudaScheduler {
public:
    explicit CudaScheduler(async_task::gpu::IGpuDriver* driver = nullptr);
    ~CudaScheduler();

    CudaScheduler(const CudaScheduler&) = delete;
    CudaScheduler& operator=(const CudaScheduler&) = delete;

    int initialize(bool stub_mode = false);
    void shutdown();
    bool is_initialized() const { return initialized_; }

    // ... 既有 submit_mem_alloc / submit_memcpy_h2d / submit_launch / wait_fence 等 ...

private:
    async_task::gpu::IGpuDriver* driver_{nullptr};  // 替换 CudaStub* stub_
    bool owns_driver_{false};

    MemoryManager memory_mgr_;
    sync::SyncManager sync_mgr_;

    std::map<uint64_t, Task> pending_tasks_;
    mutable std::mutex tasks_mutex_;

    std::atomic<uint64_t> next_task_id_{1};
    std::atomic<uint64_t> next_fence_id_{1};

    bool initialized_{false};

    uint64_t allocate_fence_id();
    uint64_t allocate_task_id();
};

}
```

### `MockGpuDriver` 测试夹具

```cpp
// tests/mock_gpu_driver.hpp
namespace async_task {
namespace gpu {

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

    // 28 个 IGpuDriver 方法覆盖
    // 每个方法：1. record(method, args); 2. if injected_error return sentinel; 3. return canned value

    int open() override { record("open"); return open_inject_error_ ? -1 : 0; }
    int close() override { record("close"); return 0; }
    // ... 等 28 个 ...

    // 注入错误辅助方法
    void inject_open_error(bool enable) { open_inject_error_ = enable; }
    // ... 等 28 个 inject 方法 ...

    // 录制历史访问
    const std::vector<CallRecord>& history() const { return history_; }
    void clear_history() { history_.clear(); }

private:
    std::vector<CallRecord> history_;
    bool open_inject_error_{false};
    // ... 其他 inject flags ...
    uint64_t next_handle_{1};  // 递增返回
};

}}
```

## Risks / Trade-offs

| Risk | Impact | Mitigation |
|------|--------|------------|
| **R1**: D6/D7/D8 改写破坏现有 `cuda_scheduler.cpp` / `cmd_cuda.cpp` 调用 | 中 | tasks §1 显式列出所有调用方并同步适配；编译 + 测试验证 |
| **R2**: D9 命名空间迁移破坏跨 TU `#include` | 低 | namespace alias（`taskrunner::CudaStub = async_task::gpu::CudaStub`）1 release 过渡 |
| **R3**: `MockGpuDriver` 不完整（漏实现 28 个方法中的几个）→ 编译失败 | 中 | 用 `IGpuDriver` 纯虚强制覆盖；`= default` / `= 0` 编译器提示 |
| **R4**: `init_gpu_client()` 启动失败 → CLI 直接挂 | 低 | 失败时打印警告 + 继续（保留测试模式行为） |
| **R5**: `CudaStub` mock 行为与 `GpuDriverClient` 真实行为不一致 → 切换实现时集成测试 fail | 中 | R5 Requirement 显式测试 `IGpuDriver*` 注入路径；切换实现时跑同一组 test cases |
| **R6**: H-1 既有 `setCurrentVASpace()` 调用方未迁移到 snake_case | 低 | 保留 deprecated alias 1 release；tasks §8 记录 |
| **R7**: 5 个 H-3 占位方法抛异常 → 误用时 runtime crash | 中 | 文档显式标注 "H-3 范围"；`CudaStub` mock 返回 0 而非抛异常（mock 语义）|
| **R8**: `CudaScheduler(CudaStub*)` 旧调用方（CudaStub 在 taskrunner 命名空间）与 `CudaScheduler(IGpuDriver*)` 新签名歧义 | 低 | namespace alias 让 CudaStub 唯一；隐式转换到 IGpuDriver* |

## Migration Plan

### Phase 1: TaskRunner 实现（5 个 Section，并行或顺序均可）

按 `tasks.md` §1-§6 顺序执行：
1. `GpuDriverClient` 加 `IGpuDriver` 继承 + D6/D7/D8 对齐
2. `CudaStub` 命名空间迁移 + IGpuDriver 继承
3. `CudaScheduler` DI 重构
4. `MockGpuDriver` 测试夹具
5. CLI `init_gpu_client()` 死调用修复
6. 测试（既有 8 case 不回归 + 新 MockGpuDriver 注入测试）

### Phase 2: 跨仓同步（UsrLinuxEmu）

```bash
cd /workspace/project/UsrLinuxEmu
# 1. 激活 openspec change
mv /workspace/project/UsrLinuxEmu/external/TaskRunner/plans/2026-06-19-h2-5-architecture-foundation \
   /workspace/project/UsrLinuxEmu/openspec/changes/h2-5-architecture-foundation
# 2. 移除 DRAFT 标记（README + .openspec.yaml）
# 3. 更新子模块指针
git add external/TaskRunner
# 4. 提交
git add openspec/changes/h2-5-architecture-foundation/
git commit -m "feat(h2-5): IGpuDriver abstraction + 2 implementations + DI + Mock + CLI fix"
```

### Phase 3: 验证 + 归档

```bash
# TaskRunner 独立构建
cd external/TaskRunner && cd build && make -j4
./test_cuda_scheduler   # 8/8 pass（无回归）
./test_gpu_architecture # 新增 N/N pass

# UsrLinuxEmu build（main）
make -j4 && ctest
bash tools/docs-audit.sh --strict

# 归档
openspec archive h2-5-architecture-foundation
git add openspec/changes/archive/2026-06-19-h2-5-architecture-foundation/
git commit -m "chore(openspec): archive h2-5-architecture-foundation"
```

### Rollback

| 阶段 | 回滚命令 |
|------|---------|
| Phase 1 任一节失败 | `git reset HEAD~1 && git restore .` (TaskRunner 仓，未 push) |
| Phase 1 push 后 | `git push --force-with-lease` 回退 + revert PR |
| Phase 2 失败 | `git restore --staged openspec/changes/ external/TaskRunner` |
| Phase 3 archive | `rm -rf openspec/changes/archive/2026-06-19-h2-5-architecture-foundation/` |

各阶段独立可逆。

## Open Questions

无（D6-D11 已 FINALIZED，无 TBD 项）。

H-3 上游 owner-flagged 3 issue（stream_id u32 类型不匹配 / ioctl 绕过 GpuQueueEmu / attached_queues 弱校验）已交叉引用到 **UsrLinuxEmu H-7 ADR**，TaskRunner 侧**不**解决。