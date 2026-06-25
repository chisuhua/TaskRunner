---
SCOPE: TEST-FIXTURE
STATUS: ACCEPTED
---

# Phase 1 — CUDA Runtime 兼容 MVP

> **状态**: ✅ 完成 (2026-04-29)
> **周期**: 2026-04-07 → 2026-04-29 (3 周)
> **关联**: UsrLinuxEmu PR #6 (`c40a149`) 回引合并

## 目标

构建 TaskRunner + UsrLinuxEmu 基座之上的 CUDA Runtime API 兼容层 MVP，跑通 `cudaMalloc` / `cudaMemcpy` / `cudaLaunchKernel` 端到端。

## 验收标准

- ✅ `cudaMalloc` 返回 device pointer
- ✅ `cudaMemcpy` 走 ioctl 路径成功
- ✅ `cudaLaunchKernel` 提交到 UsrLinuxEmu 并被 `BasicGpuSimulator` 执行
- ✅ `test_cuda_scheduler` 8/8 通过

## 关键同步点

| 同步点 | 状态 | 日期 |
|--------|------|------|
| S0 (接口冻结) | ✅ | 2026-04-28 |
| S1 (ioctl 路径打通) | ✅ | 2026-04-28 |
| S2 (BasicGpuSimulator 集成) | ✅ | 2026-04-28 |
| S3 (CudaScheduler 集成) | ✅ | 2026-04-28 |
| S4 (CLI MVP) | ✅ | 2026-04-29 |

## 关键 commit

| Commit | 主题 |
|--------|------|
| `f782535` | feat: DDS v1.2 CUDA scheduler implementation |
| `371cef5` | feat(gpu): implement GpuDriverClient System C wrapper |

## 架构决策

| 决策 | TADR | 状态 |
|------|------|------|
| 集成路径选择（B 统一调度器）| [TADR-001](../adr/tadr-201-unified-scheduler.md) | ✅ Accepted (retroactive) |
| 接口扩展策略（C 分层设计）| [TADR-002](../adr/tadr-202-layered-design.md) | ✅ Accepted (retroactive) |
| Stub 独立追踪（B 方案）| [TADR-004](../adr/tadr-101-stub-tracker.md) | ✅ Accepted (retroactive) |

## 测试基线

- `test_cuda_scheduler`: ✅ 8/8

## v0.1 提案 deviation

| 提案 (TADR-001) | 实施 | 偏差 |
|----------------|------|------|
| UnifiedScheduler B 方案 | 简化为 `CudaScheduler` (单 scheduler) | ✅ 简化（避免过度设计） |
| 4 种基础 DeviceCommand | 保留 2 种 (KERNEL/DMA_COPY)，Phase 2 ioctl 编号扩展 | ✅ 推迟到 Phase 2 |
| CommandTranslator 转译层 | 未实施，由 `cuda_stub.cpp` 直接编码 `gpu_gpfifo_entry` | ✅ 简化 |

详见 [retrospective.md](./retrospective.md)

---

**最后更新**: 2026-06-23（H-4.5 docs governance cleanup 整理 phase 文档）
