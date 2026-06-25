---
SCOPE: TEST-FIXTURE
STATUS: ACCEPTED
---

# Phase 2 — IGpuDriver 抽象 + Phase 2 Lifecycle

> **状态**: ✅ 完成 (2026-06-23)
> **周期**: 2026-06-17 → 2026-06-23 (1 周)
> **关联**: UsrLinuxEmu commits `c64301c` (S5 foundation) + TaskRunner commits `241f3ed..8625b82`

## 目标

H-2.5 + H-3 联合交付：
1. **H-2.5**：引入 `IGpuDriver` 抽象接口作为 TaskRunner ↔ UsrLinuxEmu 的 GPU 驱动契约 SSOT
2. **H-3**：在 IGpuDriver 接口新增 5 个 Phase 2 方法（VA Space 创建/销毁 + GPU 绑定 + Queue 创建/销毁）

## H-2.5 — IGpuDriver 抽象层

### 验收标准
- ✅ `IGpuDriver` 接口定义 (28 虚方法, 311 行, [`include/igpu_driver.hpp`](../../include/igpu_driver.hpp))
- ✅ 3 个实现: `GpuDriverClient` (真 ioctl) + `CudaStub` (in-memory mock) + `MockGpuDriver` (headless 测试夹具)
- ✅ 命名空间迁移: `taskrunner::*` → `async_task::gpu::*`
- ✅ `CudaScheduler` 构造函数接受 `IGpuDriver*`（DI）
- ✅ CLI `init_gpu_client()` 显式调用（修复 dead call）

### 关键 commit (TaskRunner)
- `4834d5a` feat(igpu): add IGpuDriver abstract interface
- `1684fa1` feat(igpu): implement H-2.5 architecture foundation (D6-D11)

### 关键 commit (UsrLinuxEmu)
- `c64301c` (S5 architecture foundation)

### 关键决策
[TADR-005 IGpuDriver 抽象层 Consumer-Lens](../adr/tadr-102-igpu-driver.md)

### 测试
- `test_gpu_architecture`: ⚠️ 10/11 (H-2.5 Bonus 预存在 baseline)

## H-3 — Phase 2 Lifecycle

### 验收标准
- ✅ 5 Phase 2 ioctl wrapper 方法（`create_va_space` / `destroy_va_space` / `register_gpu` / `create_queue` / `destroy_queue`）
- ✅ 12 doctest cases (`tests/test_gpu_phase2.cpp`)
- ✅ 2 CLI subcommand（`cuda_va_space` / `cuda_queue`）
- ✅ R2 mapping contract LOW32 truncation 显式化
- ✅ H-3.5 follow-up 提示（T6-T9 mock-behavior deviation）

### 关键 commit (TaskRunner 9 commits)
```
241f3ed feat(igpu): implement 5 Phase 2 methods on GpuDriverClient (H-3)
25e370d refactor(igpu): move doorbell comment before return in create_queue
9a5b68e feat(igpu): implement 5 Phase 2 mock methods on CudaStub
6aec021 fix(igpu): add va_space_handle==0 guard to CudaStub::register_gpu
0a7b59e test(igpu): add test_gpu_phase2.cpp with 10 H-3 doctest cases + 2 R2 bonus
84455ed test(igpu): clarify T6 inject_error intent
e292831 feat(cli): add cuda_va_space + cuda_queue subcommands
8625b82 refactor(cli): make R2 mapping truncation explicit in cuda_queue
```

### 关键决策 (TADR)
- [TADR-005](../adr/tadr-102-igpu-driver.md) — IGpuDriver 抽象层
- [TADR-006](../adr/tadr-103-h3-phase2.md) — Phase 2 5 方法
- [TADR-007](../adr/tadr-104-r2-mapping.md) — R2 mapping
- [TADR-008](../adr/tadr-105-h7-deferred.md) — H-7 deferred mirror

### 测试
- `test_gpu_phase2`: ✅ 12/12

## Phase 2 综合测试基线

| 测试 | 状态 | 来源 |
|------|------|------|
| `test_cuda_scheduler` | ✅ 8/8 | H-1 baseline preserved |
| `test_gpu_architecture` | ⚠️ 10/11 | H-2.5 Bonus 预存在 baseline |
| `test_gpu_phase2` | ✅ 12/12 | H-3 新增 |
| CLI smoke test | ✅ | `cuda_va_space` + `cuda_queue` 全部正常 |

## v0.1 提案 deviation

详见 [retrospective.md](./retrospective.md) — Phase 2 是 deviation 最大阶段：
- UnifiedScheduler 中央调度器 → IGpuDriver 抽象 + 3 实现 DI
- 4 种基础 DeviceCommand → ioctl 编号扩展（保留 UsrLinuxEmu 端 2 种）
- SyncSource / SyncManager → fence_id 简化同步

---

**最后更新**: 2026-06-23（H-4.5 docs governance cleanup 整理 phase 文档）
