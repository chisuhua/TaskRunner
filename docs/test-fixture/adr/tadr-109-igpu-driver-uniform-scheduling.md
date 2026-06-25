---
SCOPE: TEST-FIXTURE
STATUS: ACCEPTED
REPLACES: (none, new in H-3.5)
RELATED: tadr-103-h3-phase2, tadr-102-igpu-driver, openspec/changes/2026-06-25-h3-5-followup-test-fixture-cleanup
---

# TADR-109: IGpuDriver 31-Method Extension (H-3.5 follow-up)

**状态**: ✅ Accepted
**日期**: 2026-06-25
**提案人**: TaskRunner owner
**关联 Change**: `openspec/changes/2026-06-25-h3-5-followup-test-fixture-cleanup/`
**关联 TADR**: TADR-103 (H-3 Phase 2 Consumer-Lens), TADR-102 (IGpuDriver 抽象层)

---

## Context

H-2.5 D10 已声明 `CudaScheduler::driver_` 类型从 `CudaStub*` 改为 `IGpuDriver*`，但 `cuda_scheduler.cpp` 中 6 处 `dynamic_cast<async_task::gpu::CudaStub*>(driver_)` 未删除，导致：
- 注入 `GpuDriverClient` 时 legacy 路径返回 `-ENOSYS`
- 注入 `MockGpuDriver` 时编译通过但 `dynamic_cast` 失败
- 只有注入 `CudaStub` 时工作

H-3.5 设计决策：将 CudaStub-specific lifecycle 方法（`set_stub_mode` / `initialize` / `shutdown`）上移到 `IGpuDriver` 接口，让 3 个实现各自 override。

## Decision

### D1: IGpuDriver 接口扩展 3 个 lifecycle 方法（28 → 31）

```cpp
virtual void set_stub_mode(bool stub_mode) {}     // 默认 no-op
virtual int  initialize() { return 0; }            // 默认 success
virtual void shutdown() {}                          // 默认 no-op
```

**理由**：
- 默认 no-op/success 实现允许现有代码零修改（向后兼容）
- 3 个实现（CudaStub / GpuDriverClient / MockGpuDriver）按需 override
- 删除 `CudaScheduler` 的 2 处 lifecycle `dynamic_cast`（line 45, 65）

**代价**：
- 接口增加 3 个虚方法
- CudaStub::initialize() 返回类型从 `CudaResult` 改为 `int`（与接口签名一致）

### D2: 4 处 legacy `dynamic_cast` 保留（line 101, 147, 188, 227, 269）

**理由**：
- 这些是 CudaStub-only CUDA Driver API 路径（mem_alloc / memcpy_h2d/d2h/d2d / launch_kernel）
- 上移到 IGpuDriver 会污染抽象（CUDA Driver API 不是通用 GPU 概念）
- 当前 main 测试（`test_cuda_scheduler.cpp` 8 cases）依赖这些路径
- H-3.5 不破坏现有功能，遗留问题留待未来 H-3.6+ 决定

### D3: MockGpuDriver 5 Phase 2 guards 与 GpuDriverClient / CudaStub 一致

| 方法 | guard 行为 | 与 GpuDriverClient 一致? |
|------|-----------|--------------------------|
| `create_va_space(0)` | 检查 `is_open_`，false 返回 0 | ✅（GpuDriverClient 用 fd_ 检查）|
| `destroy_va_space(0)` | 返回 -1（不 log）| ✅ |
| `register_gpu(0, ...)` | 返回 -1（不 log）| ✅ |
| `create_queue(0, ...)` | 返回 0（不 log）| ✅ |
| `destroy_queue(0)` | 返回 -1（不 log）| ✅ |

**理由**：T6-T9 mock-behavior deviation 关闭（之前验证 mock canned value，现在验证 guard rejection）。

### D4: test_gpu_architecture.cpp 回归修复

删除 `CHECK_THROWS`（与 H-3 spec L14-29 + L52-57 + L70-73 + L93-96 + L120-123 矛盾），改为 `CHECK_NOTHROW + CHECK(result == 0/-1)`。

## Consequences

### 正面
- ✅ CudaScheduler 抽象泄漏部分修复（6 dynamic_cast → 4 legacy + 2 lifecycle removed）
- ✅ MockGpuDriver 与 GpuDriverClient / CudaStub guard 行为一致
- ✅ T6-T9 测试真正验证 guard rejection
- ✅ IGpuDriver 接口扩展向后兼容（默认 no-op）

### 负面 / 风险
- ⚠️ 4 处 legacy dynamic_cast 保留（mem_alloc / memcpy_* / launch_kernel）
- ⚠️ CudaStub::initialize() 返回类型变更（breaking change for 直接调用方，但 IGpuDriver 调用方已统一）

## 实施 commits（H-3.5 single commit chain）

TaskRunner 仓 commit `5ff8c26`：
- `include/shared/igpu_driver.hpp:305-338` — 3 lifecycle 方法
- `include/test_fixture/cuda_stub.hpp:108-128` — override 关键字
- `src/test_fixture/cuda_stub.cpp:31-43` — initialize 返回 int
- `tests/test_fixture/mock_gpu_driver.hpp:74-87, 252-289, 354-360` — 3 lifecycle + 5 guards + 3 成员变量
- `src/test_fixture/cuda_scheduler.cpp:35-50, 62-65` — 删除 2 dynamic_cast
- `tests/test_fixture/test_gpu_architecture.cpp:203-232` — 回归修复
- `tests/test_fixture/test_gpu_phase2.cpp:33-43, 112-167` — T1 修复 + T6-T9 更新
- `docs/test-fixture/adr/tadr-103-h3-phase2.md` — Completion 段

## 验证结果

| 测试 | 结果 |
|------|------|
| `test_cuda_scheduler` | 8/8 PASS |
| `test_gpu_architecture` | 11/11 PASS（回归修复）|
| `test_gpu_phase2` | 12/12 PASS（T6-T9 guard verification + T1 fix）|
| `cmake -DTASKRUNNER_BUILD_MODE=umd-evolution` | 100% Built |

## 跨引用

- **TADR-103**: §H-3.5 Completion 段（实施详情）
- **TADR-102**: IGpuDriver 抽象层（28 → 31 方法演进）
- **openspec**: `openspec/changes/archive/2026-06-25-h3-5-followup-test-fixture-cleanup/`
- **spec**: `gpu-phase2-management` capability（H-3.5 扩展 3 个 ADDED Requirements）