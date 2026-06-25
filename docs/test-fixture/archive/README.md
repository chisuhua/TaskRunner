---
SCOPE: TEST-FIXTURE
STATUS: DEPRECATED
---

# docs/archive/ — 历史快照与废弃文档

> **最后更新**: 2026-06-23（H-4.5 docs governance cleanup 建立归档区）

归档区用于存放：
1. **已废弃的架构提案** — 被新架构超越的 v0.1 设计文档
2. **已 capture 到 TADR 的决策框架** — 保留作历史快照
3. **已 shippable change 的设计规范** — 早期 DDS，被后续 H-x 重构

## 归档政策

- 文件**原状保留**（不修改正文）
- 文件头加 `⚠️ DEPRECATED` 块，指向取代它的 TADR / 新文档
- 命名规范：`YYYY-MM-DD-<original-name>.md`（原日期 + 原名）

## 当前归档

| 原文件 | 归档日期 | 行数 | 归档原因 | 取代关系 |
|--------|---------|----:|---------|---------|
| `2026-04-07-decision-frame-cuda-vulkan-runtime.md` | 2026-04-07 | 306 | D1-D4 决策已 capture 到独立 TADR | [TADR-001~004](../adr/README.md#索引) |
| `2026-04-07-cuda-vulkan-runtime-architecture.md` | 2026-04-07 | 680 | v0.1 提案，从未作为类实施 | [architecture/current.md](../architecture/current.md) + [roadmap/retrospective.md](../roadmap/retrospective.md) |
| `2026-04-07-DDS-CUDA-Vulkan-Runtime-v1.2-final.md` | 2026-04-07 | 688 | v1.2 详细设计规范，被 H-2.5/H-3 超越 | [TADR-005](../adr/tadr-102-igpu-driver.md) + [TADR-006](../adr/tadr-103-h3-phase2.md) |

## 引用规范

在 docs/ 其他文档或 ADR 中引用归档文件时：

```markdown
历史快照：[archive/2026-04-07-decision-frame-cuda-vulkan-runtime.md](./2026-04-07-decision-frame-cuda-vulkan-runtime.md)
```

请同时引用对应的取代文档（如 TADR 或新架构文档），避免读者仅依赖过时快照。

---

**维护者**: TaskRunner owner
