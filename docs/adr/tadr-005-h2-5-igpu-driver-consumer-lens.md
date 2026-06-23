# TADR-005: IGpuDriver 抽象层 Consumer-Lens (H-2.5)

**状态**: ✅ Accepted
**日期**: 2026-06-23
**提案人**: TaskRunner owner 协同 (Sisyphus session)
**评审者**: UsrLinuxEmu Architecture Team + TaskRunner owner
**关联 ADR (UsrLinuxEmu)**: [ADR-032](../../../../docs/00_adr/adr-032-h2-5-igpu-driver-abstraction.md) (canonical)
**关联 Change**: `openspec/changes/archive/2026-06-19-h2-5-architecture-foundation/`
**关联 Source**: `openspec/changes/archive/2026-06-19-h2-5-architecture-foundation/design.md` §D6-D11

---

## Context

H-2.5 引入 `IGpuDriver` 抽象接口作为 TaskRunner ↔ UsrLinuxEmu 的 GPU 驱动契约 SSOT。上游决策由 UsrLinuxEmu ADR-032 记录（见 §Decision D6-D11）。

本 TADR 记录 **TaskRunner 侧的落地细节**（3 个实现覆盖 28 方法 + 依赖注入 + CLI 修复），不重复 ADR-032 的上游决策文本。

## Decision

### 3 个 IGpuDriver 实现

| 实现 | 文件 | 语义 |
|------|------|------|
| **`GpuDriverClient`** | [`include/gpu_driver_client.h`](../../gpu_driver_client.h) (576 行) + `src/gpu_driver_client.cpp` | 真实 ioctl 实现，通过 `/dev/gpgpu0` 通信 |
| **`CudaStub`** | [`include/cuda_stub.hpp`](../../cuda_stub.hpp) (238 行) + `src/cuda_stub.cpp` (414 行) | In-memory mock，单测默认 |
| **`MockGpuDriver`** | `tests/mock_gpu_driver.hpp` | Headless 测试夹具（含 `history()` + `inject_error()`)|

3 个实现均 `override` IGpuDriver 的 28 个虚方法（接口定义见 [`include/igpu_driver.hpp`](../../igpu_driver.hpp)，311 行）。

### 依赖注入（D10 落地）

`CudaScheduler` 构造函数：

```cpp
class CudaScheduler {
public:
    explicit CudaScheduler(async_task::gpu::IGpuDriver* driver = nullptr);
private:
    async_task::gpu::IGpuDriver* driver_;  // may own CudaStub if nullptr
};
```

- `nullptr` 默认值 → 构造时自动 `new CudaStub()`（向后兼容）
- 注入 `MockGpuDriver` → 单元测试隔离
- 注入 `GpuDriverClient` → CLI 生产路径

### CLI 死调用修复（D11 落地）

`cli_main.cpp` 入口处显式调用 `init_gpu_client()` 初始化 `g_gpu_client`，否则所有 CLI subcommand 走 stub mode silent fallback。

## Consumer-Lens

### 实施 commit 链

```
4834d5a feat(igpu): add IGpuDriver abstract interface
1684fa1 feat(igpu): implement H-2.5 architecture foundation (D6-D11)
```

### 命名空间迁移（D9 落地）

- 旧 `taskrunner::CudaStub` → `async_task::gpu::CudaStub`
- 兼容策略：`namespace taskrunner { using async_task::gpu::*; }`（1 release 过渡）
- 迁移证据：`gpu_driver_client.h:41-42` 全部代码在 `async_task::gpu::` 命名空间

### Deprecated alias（H-1 兼容）

```cpp
// gpu_driver_client.h L412-421
[[deprecated("H-2.5: use set_current_va_space()")]]
void setCurrentVASpace(uint64_t va_space_handle) { set_current_va_space(va_space_handle); }
```

旧 CamelCase API 通过 alias 调用 snake_case 版本，调用方零修改。

## Consequences

### 正面

- ✅ 测试可注入 `MockGpuDriver`（11 cases 通过 `test_gpu_architecture.cpp` 验证）
- ✅ 生产可注入 `GpuDriverClient`（CLI 真驱动路径）
- ✅ H-3 5 Phase 2 ioctl wrapper 可在统一接口下实现（TADR-006）

### 负面 / 风险

- ⚠️ 新贡献者需理解 3 个实现的语义差异
- ⚠️ `fd()` 等方法仅 `GpuDriverClient` 有意义，其他实现返回 stub 值（抽象泄漏）

## 跨引用

- **上游 UsrLinuxEmu ADR-032**: §Decision D6-D11, §Migration
- **关联 TADR**: TADR-006 (H-3 Phase 2 lifecycle), TADR-007 (R2 mapping contract)
- **关联文件**:
  - [`include/igpu_driver.hpp`](../../igpu_driver.hpp) (接口 28 方法)
  - [`include/gpu_driver_client.h`](../../gpu_driver_client.h) (576 行)
  - [`include/cuda_stub.hpp`](../../cuda_stub.hpp) (238 行)
  - `tests/mock_gpu_driver.hpp`

---

**最后更新**: 2026-06-23（H-4.5 docs governance cleanup 建立 TADR-005 consumer-lens）
