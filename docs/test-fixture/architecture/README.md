---
SCOPE: TEST-FIXTURE
STATUS: ACCEPTED
---

# docs/architecture/ — TaskRunner 整体架构目录

> **范围**: TaskRunner 用户态 CUDA/Vulkan API 兼容层 + Runtime Stub
> **最后更新**: 2026-08-04 (新增 2 份 gap-analysis: phase-3-1-3-2 + stage4-ioctl-consumer)
> **关联**: [docs/adr/](../adr/) (决策记录) + [docs/roadmap/](../roadmap/) (实施路径图)

TaskRunner 架构的 **consumer-lens 视图**（与 UsrLinuxEmu 仓 SSOT 交叉引用，不重复内容）。

## 索引

| 文档 | 主题 | 状态 |
|------|------|------|
| [current.md](./current.md) | 当前架构 (IGpuDriver 抽象 + DI 模式) | ✅ Live |
| [layers.md](./layers.md) | 分层视图 (App → Stub → Scheduler → IGpuDriver → Backend) | ✅ Live |
| [data-flow.md](./data-flow.md) | 数据流图 (CUDA kernel launch / Vulkan submit / 测试注入) | ✅ Live |
| [capabilities.md](./capabilities.md) | Capability 分组 (gpu-driver-arch / gpu-phase2 / governance) | ✅ Live |
| [phase-3-1-3-2-real-path-implementation-gap-analysis.md](./phase-3-1-3-2-real-path-implementation-gap-analysis.md) | Phase 3.1/3.2 Real-Path 实施差距 (CudaStub 缺口 + umd E2E 回归) | 🔄 Proposed |
| [stage4-ioctl-consumer-gap-analysis.md](./stage4-ioctl-consumer-gap-analysis.md) | Stage 4.4-4.6 ioctl Consumer 差距 (7 类上游能力未消费) | 🔄 Proposed |

## 与 UsrLinuxEmu SSOT 关系

TaskRunner 是 UsrLinuxEmu 的 git submodule。架构权威定义在 UsrLinuxEmu [`docs/02_architecture/post-refactor-architecture.md`](../../../docs/02_architecture/post-refactor-architecture.md)（canonical SSOT）。

本目录的 consumer-lens 文档：
- **不重复** UsrLinuxEmu SSOT 内容
- **专注** TaskRunner 用户态视角（CUDA Stub / CudaScheduler / GpuDriverClient / IGpuDriver 28 方法）
- **跨引用** UsrLinuxEmu [ADR-NNN](../../../../docs/00_adr/)（上游决策）+ [TADR-NNN](../adr/)（本仓决策）

## 阅读路径建议

**新人 onboard**：
1. 先读 [layers.md](./layers.md) 了解 5 层结构
2. 再读 [current.md](./current.md) 看当前架构图
3. 最后读 [data-flow.md](./data-flow.md) 看具体数据流

**深入了解**：
- [capabilities.md](./capabilities.md) — Capability 分组（对应 UsrLinuxEmu ADR-035 §按 Capability 分组）
- 决策依据 → [docs/adr/](../adr/)

---

**维护者**: TaskRunner owner
