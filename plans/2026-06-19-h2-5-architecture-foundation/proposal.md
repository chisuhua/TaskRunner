# Change: h2-5-architecture-foundation

> **状态**: ⚠️ DRAFT — 骨架已就位，待 review 后激活
> **创建**: 2026-06-19
> **前置依赖**: **无**（本 change 是后续 H-3 的基础）
> **后续依赖**: **H-3** `h3-phase2-management` —— **本 change 完成后 H-3 才可激活**
> **关联 SSOT**: `docs/02_architecture/post-refactor-architecture.md` §1.3 (v0.1.5 待加)
> **历史**: 取代 `plans/2026-06-19-h2-phase2-openspec-skeleton/`（DEPRECATED 2026-06-19，拆分依据：Path D 重构优先）
> **激活流程**: 本 change 在 UsrLinuxEmu `openspec/changes/` 下创建正式条目

## Why

### 现状（H-1 closeout 之后）

| 侧 | 能力 |
|---|---|
| UsrLinuxEmu (kernel + GPU plugin) | Phase 2 ioctls 全部实现<br>+ H-1 PUSHBUFFER 校验链路（`bf8192f`）<br>+ 4 个测试（`09ae1b0`）<br>+ ADR-024 Accepted v1 |
| `IGpuDriver`（抽象接口） | ✅ **已起草** `include/igpu_driver.hpp`（311 行 28 个纯虚方法）<br>但**无任何实现**继承它 |
| TaskRunner `GpuDriverClient` | ✅ H-1 `setCurrentVASpace()` opt-in 透传<br>❌ **运行时 dead code** —— `g_gpu_client` 全程为 `nullptr`（`init_gpu_client()` 从未被 `cli_main.cpp` 调用）<br>❌ 签名与 `IGpuDriver` 不匹配（5 个方法需对齐） |
| TaskRunner `CudaStub` | ✅ 与 `CudaScheduler` 联调（8 个 doctest 用例 pass）<br>❌ 在 `taskrunner` 命名空间（与 `GpuDriverClient` 隔离）<br>❌ 零 VA Space / Queue 概念 |
| `GpuDriverClient` ↔ `CudaScheduler` | ❌ **解耦但不连通** —— 两者分属不同抽象，无共同接口 |

### 调用链断点

```
[TaskRunner CLI] → cmd_cuda_alloc/memcpy/launch/wait
                          ↓
                   g_gpu_client (恒为 nullptr!)  // ← D11 死调用
                          ↓
                   [真实路径不存在，走 CudaStub 假路径]
                          ↓
                   CudaScheduler::stub_ → CudaStub (mock)
                          ↓
                   [零 VA Space 概念，无法接 H-1 校验]
```

**Gap 1（接口缺失）**：`GpuDriverClient` 与 `CudaStub` 无共同接口，`CudaScheduler` 只能选其一，无法根据场景切换（联调真 GPU vs 单测 mock）。

**Gap 2（运行时断点）**：CLI 入口 `cli_main.cpp` (43 行) 委托给 `cmd_buffer_v2_main`，后者依赖 `g_gpu_client`，但 `init_gpu_client()` 在 `src/gpu_driver_client.cpp` 中实现却**从未被调用**。

**Gap 3（架构断点）**：H-2.5 之前即使 `IGpuDriver` 已起草，**两实现 + 一 DI + 一 Mock** 都未就位，`CudaScheduler` 既不能注入 `GpuDriverClient`（无 `IGpuDriver` 继承）也不能注入 `MockGpuDriver`（无此类型）。

### Why Now

1. **H-1 已就绪**：校验链路可被触发，但调用方架构未对齐，H-1 价值打折
2. **H-3 即将开始**：H-3 的 5 个 Phase 2 ioctl wrapper 必须建立在 H-2.5 抽象之上，无 H-2.5 则 H-3 无注入点
3. **PRD 2 季度目标**：Phase 2 是 UsrLinuxEmu 路线图的 v0.2 里程碑，TaskRunner 必须配套
4. **Path D 决策**：2026-06-19 选定 "重构优先"，本 change 是 Path D 的第一个产出
5. **避免测试架构债**：现有 `tests/test_cuda_scheduler.cpp` 测的是 `CudaScheduler + CudaStub`，**不**测 `GpuDriverClient`；新 `MockGpuDriver` 必须现在就位，否则 H-3 测试无法注入

## What Changes

### 1. `GpuDriverClient` 实现 `IGpuDriver`（按 D6/D7/D8 对齐签名）

```cpp
// include/gpu_driver_client.h
namespace async_task {
namespace gpu {

class GpuDriverClient : public IGpuDriver {  // ← 新增继承
public:
    // ... 既有方法按 D6/D7/D8 改写 ...

    // D6: alloc_bo 签名
    uint64_t alloc_bo(uint64_t size, uint32_t flags) override;

    // D7: free_bo handle 类型拓宽
    int free_bo(uint64_t bo_handle) override;

    // D8: map_bo 签名重塑
    void* map_bo(uint64_t bo_handle, uint64_t size) override;

    // ... 其余 24 方法按 IGpuDriver 覆盖 ...
};

}}  // namespace async_task::gpu
```

### 2. `CudaStub` 迁移到 `async_task::gpu` 命名空间 + 实现 `IGpuDriver`（按 D9）

```cpp
// include/cuda_stub.hpp  (命名空间从 taskrunner → async_task::gpu)
namespace async_task {
namespace gpu {

class CudaStub : public IGpuDriver {  // ← 新增继承 + 命名空间迁移
public:
    // ... 既有 CUDA Driver API 封装方法 ...
    // ... 新增 IGpuDriver 全部 28 方法覆盖（按 mock 语义）...
};

}}
```

**向后兼容（1 release 过渡）**：
```cpp
// 在旧 namespace 提供 alias
namespace taskrunner {
    namespace gpu = async_task::gpu;
    using CudaStub = async_task::gpu::CudaStub;  // 旧调用方零修改
}
```

### 3. `CudaScheduler` 构造函数加 `IGpuDriver*` 注入（按 D10）

```cpp
// include/cuda_scheduler.hpp
namespace taskrunner {

class CudaScheduler {
public:
    // D10: 接受 IGpuDriver* 注入 + nullptr 时 auto-create CudaStub
    explicit CudaScheduler(async_task::gpu::IGpuDriver* driver = nullptr);
    // ...
private:
    async_task::gpu::IGpuDriver* driver_{nullptr};  // 替换 CudaStub* stub_
    bool owns_driver_{false};  // 是否拥有 driver 所有权
};

}
```

**向后兼容**：现有 `CudaScheduler(CudaStub*)` 调用方仍可用（CudaStub 实现 IGpuDriver → 隐式转换）；无参构造仍自动 `new CudaStub()`。

### 4. `MockGpuDriver` 测试夹具（新增）

```cpp
// tests/mock_gpu_driver.hpp  (新文件)
namespace async_task {
namespace gpu {

class MockGpuDriver : public IGpuDriver {
public:
    // 28 个 IGpuDriver 方法覆盖
    // 每个方法：
    //   1. 记录调用到 std::vector<CallRecord>
    //   2. 返回 canned value（可注入错误）

    void inject_alloc_bo_error(int errcode) { /* 注入错误路径 */ }
    // ... 其他 27 个 inject 方法 ...
};

}}
```

### 5. CLI 死调用修复（按 D11）

```cpp
// src/cli_main.cpp
extern int init_gpu_client();
extern void shutdown_gpu_client();
extern async_task::gpu::GpuDriverClient* g_gpu_client;

int main(int argc, char* argv[]) {
    // ... 既有 argv 解析 ...

    // D11: 启动时调用 init_gpu_client()
    if (init_gpu_client() != 0) {
        std::cerr << "Failed to init GPU client\n";
        return 1;
    }

    int ret = cmd_buffer_v2_main(argc, argv);

    // D11: 退出时调用 shutdown_gpu_client()
    shutdown_gpu_client();
    return ret;
}
```

### 6. 同步点 S5 关闭 + SSOT v0.1.5 同步

- `plans/sync-plan.md` 新增 S5 段落："✅ IGpuDriver 抽象 + 2 实现 + DI + Mock + CLI 修复"
- `AGENTS.md` "Phase 1.5 进度" section 增加 S5 ✅ Architecture foundation (2026-06-XX)

### 7. 跨仓同步（仿 H-1 closeout 模式）

- TaskRunner 仓独立 commit + PR
- UsrLinuxEmu 仓 submodule 指针更新
- UsrLinuxEmu 仓 openspec archive（含 spec.md / .openspec.yaml 等 git tracking）

## Capabilities

### New Capabilities

- `gpu-driver-architecture`：跟踪 TaskRunner 的 `IGpuDriver` 抽象层、`GpuDriverClient` / `CudaStub` 两个实现、`CudaScheduler` DI、`MockGpuDriver` 测试夹具、CLI 死调用修复。一旦所有 8 个 ADDED Requirements 满足，本 capability 可归档。

### Modified Capabilities

- **不修改** `gpu-pushbuffer-validation` capability（H-1 主能力，行为层未变）
- **不修改** `gpu-pushbuffer-validation-deployment` capability（H-1 closeout，部署层）

## Impact

| 影响项 | 范围 | 风险 |
|--------|------|------|
| `IGpuDriver` 抽象（已起草）| H-2.5 在 `GpuDriverClient` / `CudaStub` 上加 28 方法实现 | **低**：纯新增方法覆盖，接口形状已稳定 |
| `GpuDriverClient` 签名 | 4 个 BO 方法按 D6/D7/D8 改写 | **中**：破坏现有调用方（`src/cuda_scheduler.cpp` / `src/cmd_cuda.cpp`），需同步适配 |
| `GpuDriverClient` 全局指针 | `g_gpu_client` 从恒为 `nullptr` 变为可用 | **低**：D11 修复后自然生效 |
| `CudaStub` 命名空间 | `taskrunner` → `async_task::gpu` | **中**：跨 TU 引用需更新；alias 过渡 1 release |
| `CudaStub` 实现 `IGpuDriver` | 新增 28 方法覆盖 | **低**：mock 语义，零 ioctl |
| `CudaScheduler` 构造函数 | `CudaStub*` → `IGpuDriver*` | **低**：向后兼容（auto-create + 隐式转换）|
| ABI 兼容性 | `gpu_ioctl.h` / `gpu_types.h` **不修改** | **零**：上游 SSOT 不变 |
| 编译产物 | `libtaskrunner.a` / `test_cuda_scheduler`（8 case 不变）/ `test_gpu_architecture`（新增）| **低**：增量编译时间 |
| UsrLinuxEmu submodule | 指针更新 + openspec archive | **低**：仿 H-1 closeout 已验证流程 |
| `sync-plan.md` | 新增 S5 段落 | **零**：纯文档 |
| CLI 入口 | `cli_main.cpp` 修复 `init_gpu_client()` 死调用 | **低**：纯增量 4 行代码 |

## 交叉引用

- **H-3**（后续）: `plans/2026-06-19-h3-phase2-openspec-skeleton/` —— 本 change 是其前置；本 change 完成后 H-3 才可以激活
- **H-1**（已就绪）: UsrLinuxEmu `fix-gpu-pushbuffer-va-space-validation` —— VA Space 校验链路
- **H-1 closeout**（已就绪）: 跨仓同步模式参考
- **IGpuDriver 已起草**: `include/igpu_driver.hpp`（2026-06-22，311 行 28 个纯虚方法）
- **DEPRECATED H-2**（历史）: `plans/2026-06-19-h2-phase2-openspec-skeleton/` —— 本 change 的拆分源