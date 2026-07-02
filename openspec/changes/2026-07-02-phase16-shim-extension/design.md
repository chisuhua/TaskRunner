# Design: phase16-shim-extension

> **Change 类型**: 实施型（实现修复 + 测试扩展 + 跨仓 PR 准备 + 设计文档）
> **依赖**: commit d8ca3d3（Phase 2 drift hotfix）✅ DONE
> **Scope**: umd-evolution + 跨仓 PR 描述

## 数据流

```
CUDA Application (uses cu* via libcudart.so)
    ↓ LD_PRELOAD hook
libcuda_taskrunner.so (79 → 79 exported symbols)
    ├── 79 REAL_IMPL (post-hotfix d8ca3d3)
    │     ├── 76 true implementations
    │     └── 3 fake REAL_IMPL (cuModuleLoadData/Ex/FatBinary — to be demoted in A.2)
    ├── 65 STUB (post-hotfix)
    │     └── 68 STUB after A.2 demotion (+3)
    ↓
CudaRuntimeApi → CudaScheduler (Phase 1.5 dynamic_cast fixed) → IGpuDriver (frozen)
```

## 关键决策

### D1: cuMemGetInfo 数据源选择

**当前**：硬编码 `*free = 4 GB; *total = 8 GB`

**选项 A**（推荐）：通过 DI 注入的 IGpuDriver 接口查询真实设备信息
- 需要 GpuDriverClient 实现 `get_device_info()` 虚拟方法
- 已有类似方法 `get_device_count()` 等（tadr-301 列出 31 方法）
- 优点：与 shim 其他路径一致（通过 IGpuDriver 抽象）

**选项 B**：硬编码 fallback + env var override
- `TASKRUNNER_GPU_MEM_SIZE` 控制（默认 8GB）
- 优点：简单，无跨仓依赖
- 缺点：仍是 fake-success，不是真实硬件数据

**选项 C**：返回 `CUDA_ERROR_NOT_SUPPORTED`（诚实语义）
- 优点：彻底消除 fake-success
- 缺点：CUDA 应用程序无法查询内存信息（可能影响其行为）

**选择**：**选项 A**（GpuDriverClient::get_device_info），如果不可用则 fallback 到 **选项 B**（env var），再不行则 **选项 C**。

### D2: cuModuleLoad*Data demote 策略

**选项 A**：从 CRITICAL_APIS_IMPL_REQUIRED 移除 3 项
- 优点：诚实标记（cu_stub_table.inc 标注 STUB）
- 缺点：docs-audit Check 9 critical API count 减少（79 → 76）

**选项 B**：保留 REAL_IMPL 标记，.cpp 实现返回 NOT_IMPLEMENTED
- 优点：检查表稳定
- 缺点：仍是假 REAL_IMPL（不诚实）

**选择**：**选项 A**。诚实优先于表稳定。docs-audit 改动可通过单独 commit 反映。

### D3: 测试优先级排序

按 ROI 排序（前 3 个优先）：
1. **cuEventRecord/Synchronize**（核心同步原语，影响所有依赖事件的代码）
2. **cuStreamCreate/Synchronize**（流管理基础）
3. **cuCtxGet/SetCacheConfig**（上下文配置，影响 kernel launch 行为）
4. cuLaunchCooperativeKernel NOT_SUPPORTED 验证
5. cuStreamWaitEvent
6. cuCtxGet/SetSharedMemConfig
7. cuCtxGet/SetLimit
8. cuStreamQuery

目标 +10 tests（不是 13，覆盖核心 8 个 API，每个 1-2 个测试）。

### D4: H-3 跨仓 PR 描述范围

**选项 A**：完整 4 项 fix（全面）
**选项 B**：仅 F1（MEDIUM，最关键）+ F2（MEDIUM）
**选项 C**：仅文档级 fix（F1/F2/F3/F4 全部 docs only）

**选择**：**选项 C**。所有 4 项都是 docs 级别修改，无代码改动风险。生成完整 PR 描述文档，让 UsrLinuxEmu owner 一键采纳。

### D5: Phase 3 skeleton 命名

**选项 A**：`phase3-prep-design-notes.md`
**选项 B**：`phase3-launch-readiness.md`
**选项 C**：`phase3-trigger-conditions.md`

**选择**：**选项 A**。最中性，避开与 `phase-3-deferred.md` 命名混淆。

## 验证矩阵

| 步骤 | 测试/命令 | 预期结果 | 负责方 |
|------|----------|---------|--------|
| 前置 Gate | commit d8ca3d3 存在 | git log shows d8ca3d3 | 已有 |
| A.1 实现 | cuMemGetInfo 调用后返回真实数据 | free ≤ total；total 来自 GpuDriverClient | 本 change |
| A.1 fallback | TASKRUNNER_GPU_MEM_SIZE 未设时使用默认 8GB | total = 8 GB | 本 change |
| A.2 demote | cu_stub_table.inc 重新生成后 STUB=68, REAL_IMPL=76 | grep -c 验证 | 本 change |
| A.3 测试 | ./build/test_cuda_shim | ≥47 cases PASS (37 + 10) | 本 change |
| 全套测试 | test_cuda_* + test_gpu_* | ≥87 cases PASS | 本 change |
| docs-audit | ./tools/docs-audit.sh | 54 PASS, 0 FAIL（Critical APIs = 76） | 本 change |
| 跨仓 PR | UsrLinuxEmu owner 收到 PR 描述 | 4 项 fix 采纳 | UsrLinuxEmu owner |
| Phase 3 prep | docs/superpowers/plans/2026-07-02-phase3-prep-design-notes.md | exists, DRAFT | 本 change |
| ABI | nm | 79 symbols (unchanged) | 本 change |

## 风险缓解

| 风险 | 概率 | 影响 | 缓解 |
|------|------|------|------|
| GpuDriverClient::get_device_info() 不存在或返回错误 | M | A.1 失败 | 多层 fallback（IGpuDriver → env var → NOT_SUPPORTED） |
| cuModuleLoad*Data demote 影响其他代码路径 | L | 回归测试失败 | 全套 87+ 测试验证 |
| H-3 PR 描述被 UsrLinuxEmu owner 拒绝 | L | 文档无果 | PR 描述保留为内部 reference，未来再试 |
| Phase 3 skeleton 与未来实际设计偏差 | M | kickoff 时需重写 | DRAFT 状态，明确标注 "no implementation, design reservation only" |
| 测试用例与现有实现冲突 | L | 测试 FAIL | 每个新测试单独验证，与现有 37 测试一同跑 |