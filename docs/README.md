# docs/ — TaskRunner 文档目录

> **范围**: TaskRunner 用户态 CUDA/Vulkan API 兼容层 + Runtime Stub
> **最后更新**: 2026-06-23（H-4.5 docs governance cleanup）

TaskRunner 文档目录按用途分 4 个子目录 + 顶层历史文件。

## 文档地图

```
docs/
├── README.md (本文件)         顶层索引
│
├── adr/                      ADR 决策记录 (TADR-NNN 独立编号)
│   ├── README.md             TADR 索引 + 命名空间
│   ├── tadr-000-template.md  MADR 模板
│   └── tadr-001~008          8 个决策记录
│
├── architecture/             整体架构目录
│   ├── README.md             架构索引
│   ├── current.md            当前架构 (IGpuDriver + DI 模式)
│   ├── layers.md             分层视图
│   ├── data-flow.md          数据流图
│   └── capabilities.md       Capability 分组
│
├── roadmap/                  实施路径图
│   ├── README.md             路线图索引
│   ├── phase-1.md            Phase 1: CUDA MVP ✅
│   ├── phase-1.5.md          Phase 1.5: fence_id + va_space_handle ✅
│   ├── phase-2.md            Phase 2: IGpuDriver + Phase 2 lifecycle ✅
│   ├── phase-3.md            Phase 3: Multi-GPU / P2P ⏸️ Deferred
│   └── retrospective.md      实际 vs v0.1 提案 deviation
│
├── archive/                  归档区
│   ├── README.md             归档政策
│   ├── 2026-04-07-cuda-vulkan-runtime-architecture.md  v0.1 提案 (680 行)
│   ├── 2026-04-07-decision-frame-cuda-vulkan-runtime.md  D1-D4 决策框架 (306 行)
│   └── 2026-04-07-DDS-CUDA-Vulkan-Runtime-v1.2-final.md  v1.2 详细设计 (688 行)
│
├── plan.md                   (历史) 总计划 prompt
└── phase1-week1-plan.md      (历史) Phase 1 周计划 ✅
```

## 快速导航

| 我想... | 看... |
|---------|------|
| 了解 TaskRunner 当前架构 | [architecture/current.md](./architecture/current.md) |
| 了解某个架构决策的来龙去脉 | [adr/](./adr/) + 对应 TADR |
| 了解实施进度与未来计划 | [roadmap/](./roadmap/) |
| 查阅 4 月 v0.1 提案 | [archive/](./archive/) |
| 了解命名规范 | [AGENTS.md](../AGENTS.md) |

## 跨仓关联

TaskRunner 是 UsrLinuxEmu 的 git submodule。权威架构定义在 UsrLinuxEmu `docs/02_architecture/post-refactor-architecture.md`，本目录的 consumer-lens 文档与 UsrLinuxEmu ADR-NNN 交叉引用（详见各 TADR）。

---

**维护者**: TaskRunner owner
**最后更新**: 2026-06-23（H-4.5 docs governance cleanup 建立 4 目录结构）
