---
SCOPE: UMD-EVOLUTION
STATUS: ACTIVE
LAST_UPDATED: 2026-08-20
RELATED: tadr-308 (TaskRunner), ADR-090 v2 (UsrLinuxEmu), ADR-0029 §D8 (PTX-EMU)
ORACLE_R2: ses_fdffa6689ffeQ0vsWd06LuQgbg
MEETING_TYPE: 跨仓 owner 协调会议 (Phase 0 BLOCKING GATE 启动会)
---

# tadr-308 Phase 0 BLOCKING GATE — Owner 协调会议议程

> **⚠️ 状态**: 本文档是 **会议议程草稿**，由 TaskRunner owner 召集跨仓 owner 会议时使用。
> **会议类型**: 同步会议（≤ 90 分钟，video conference）
> **议程优先级**: 🔴 HARD blocker 优先（#1 #2 #3）

---

## 📋 会议基本信息

| 项目 | 值 |
|---|---|
| **会议主题** | tadr-308 Phase 0 BLOCKING GATE 启动协调 |
| **触发** | TaskRunner tadr-308 (Per Oracle R2 verdict) 需 6 项跨仓协调 |
| **时长** | ≤ 90 分钟 |
| **召集方** | TaskRunner owner |
| **主持** | TaskRunner owner |
| **记录** | TaskRunner owner（同步到 [TaskRunner #10](https://github.com/chisuhua/TaskRunner/issues/10)） |
| **必到** | UsrLinuxEmu owner + PTX-EMU owner + CppTLM maintainer |
| **可邀** | Oracle ses_fdffa6689ffeQ0vsWd06LuQgbg author（如需 Q&A） |

## 📋 议程（90 分钟）

### 第 1 部分：Context (10 分钟)

| 时间 | 主题 | 主讲 |
|---|---|---|
| 0:00-0:05 | tadr-308 状态 + 5 项 owner 决策已落地 | TaskRunner owner |
| 0:05-0:10 | Oracle R2 + Metis 揭示的 3 CRITICAL + Phase 0 6 项 | TaskRunner owner |

**材料**：[tadr-308](../shared/adr/tadr-308-igpu-driver-vram-load.md) + [roadmap R2](../../umd-evolution/roadmap/tadr-308-implementation-roadmap.md) §Phase 0

### 第 2 部分：🔴 HARD Blocker 决策 (50 分钟)

| 时间 | 主题 | 主讲 | 决策点 |
|---|---|---|---|
| 0:10-0:25 | **#1 `GPU_OP_DISPATCH_KERNEL = 0x10C` 定义** (TaskRunner issue draft §#1) | UsrLinuxEmu owner | ✅ 批准 / ❌ 拒绝 / 🔄 修改 opcode |
| 0:25-0:45 | **#2 ioctl 0x29 handler 实现** (TaskRunner issue draft §#2) | UsrLinuxEmu owner | ✅ 接受 1-line `hal_mem_free` / 🔄 改方案 |
| 0:45-1:00 | **#3 Mode A interim consumer for 0x10C** (TaskRunner issue draft §#3) | UsrLinuxEmu owner | ✅ 选 (a)/(b)/(c) |

**材料**：每项 5 分钟介绍 + 5 分钟 Q&A + 5 分钟决策（per item）

### 第 3 部分：🟡 SOFT 协调 (20 分钟)

| 时间 | 主题 | 主讲 | 决策点 |
|---|---|---|---|
| 1:00-1:08 | **#4 Payload layout 写入 HSK-6 joint protocol** (per ADR-035 §R5.1) | TaskRunner + UsrLinuxEmu | 文档合并点 |
| 1:08-1:16 | **#5 kernel_name host-pointer 约定 ack** (user-space emulation only) | TaskRunner | ✅ ack |
| 1:16-1:20 | **#6 文档化 code BO bypass** (FREE_BO vs 0x29 不对称) | UsrLinuxEmu owner | ✅ 接受 doc |

### 第 4 部分：跨仓 commit 顺序 + timeline (10 分钟)

| 时间 | 主题 | 主讲 | 决策点 |
|---|---|---|---|
| 1:20-1:30 | 跨仓 commit 顺序（per [ADR-035 §R5.1](../../../../../docs/00_adr/adr-035-governance-policy.md) 4 步）| 全体 | ✅ 接受顺序 |

**提议顺序**：
```
i.   UsrLinuxEmu #2 (0x29 handler) ──→ ii
ii.  UsrLinuxEmu #1 (0x10C 定义) + #4-#6 文档 ──→ iii
iii. TaskRunner tadr-308 Phase 2 实施 (T-001~T-014)
iv.  TaskRunner submodule bump → UsrLinuxEmu
v.   后续: CppTLM Mode B 集成 (HSK-6 protocol)
```

## 📋 每项决策的"决策记录"模板

会议中每项 HARD blocker 决策后，会议记录中追加：

```markdown
## 决策 #N：[项目标题]

**决策**：[✅ 批准 / ❌ 拒绝 / 🔄 修改]

**owner 决策内容**：
[具体决策内容]

**估计完工日期**：[YYYY-MM-DD]

**owner**：[name]

**依赖**：[关联 item]

**反对意见**（如有）：

**下一步**：[TaskRunner owner action]
```

## 📋 会议后 TaskRunner owner 行动清单

1. **24h 内**：把会议决策写入 [TaskRunner #10](https://github.com/chisuhua/TaskRunner/issues/10) 评论
2. **48h 内**：更新 [实施路线图 R2](../../umd-evolution/roadmap/tadr-308-implementation-roadmap.md) §Phase 0 状态（"⏳ 等待 owner 决策" → "✅ 已决策"）
3. **UsrLinuxEmu 仓 commits 落地后**（per 跨仓顺序）：在 TaskRunner 评论中记录 commit SHA
4. **TaskRunner submodule bump**（Step 2 完成）：按 [ADR-035 §R5.1](../../../../../docs/00_adr/adr-035-governance-policy.md) 同步

## 📋 风险与备选

| 场景 | 应对 |
|---|---|
| UsrLinuxEmu owner 不接受 `0x10C` | 备选：`0x10D` 或其他未用 opcode |
| UsrLinuxEmu owner 拒绝实现 0x29 handler | 备选：TaskRunner 端 inline impl（绕过 HAL, per ADR-036 边界问题） |
| Mode A consumer (c) mock-only 决策遭质疑 | 备选：(b) extend `translateLaunch`（中等改动） |
| CppTLM owner 缺席 | 异步 ack via ADR-035 §R5.1 annex §E |
| 会议超时（> 90 分钟）| 拆分 HARD/SOFT 为 2 次会议 |

## 📋 会议邀请模板

```
主题：tadr-308 Phase 0 BLOCKING GATE 跨仓协调会议

时长：90 分钟

参会者：
- TaskRunner owner (召集 + 主持)
- UsrLinuxEmu owner (HARD blocker 决策)
- PTX-EMU owner (背景信息 + cpptlm_module.h 8 ABI 协调)
- CppTLM maintainer (HSK-6 协议协调)

议程：
- 0:00-0:10 Context (Oracle R2 + Metis review 摘要)
- 0:10-1:00 HARD blocker 决策 (#1 #2 #3)
- 1:00-1:20 SOFT 协调 (#4 #5 #6)
- 1:20-1:30 跨仓 commit 顺序 + timeline

预读材料：
- TaskRunner tadr-308: docs/shared/adr/tadr-308-igpu-driver-vram-load.md
- 实施路线图 R2: docs/umd-evolution/roadmap/tadr-308-implementation-roadmap.md §Phase 0
- UsrLinuxEmu issue draft: docs/07-integration/tadr-308-phase0-blocking-usrlinuxemu-issue-draft.md
- Oracle R2 session: ses_fdffa6689ffeQ0vsWd06LuQgbg

决策需求：
- #1 #2 #3 必须达成决议（否则 Phase 0 BLOCKING GATE 不通）
- #4-#6 可后续 async 跟进
```

## 📋 跟踪表（owner 决策后填写）

| # | 项目 | owner 决策 | 决策日期 | 估计完工 | 状态 |
|---|---|---|---|---|---|
| #1 | `GPU_OP_DISPATCH_KERNEL = 0x10C` | ⏳ 待会议 | | | ⏳ |
| #2 | ioctl 0x29 handler 实现 | ⏳ 待会议 | | | ⏳ |
| #3 | Mode A consumer 决策 | ⏳ 待会议 | | | ⏳ |
| #4 | HSK-6 payload 协议 | ⏳ 待会议 | | | ⏳ |
| #5 | kernel_name host-pointer ack | ⏳ 待会议 | | | ⏳ |
| #6 | code BO bypass 文档 | ⏳ 待会议 | | | ⏳ |

---

## 📋 owner 召集会议操作清单

```bash
# 1. 选时间（所有 owner 时区可用窗口，< 90 分钟）
# 2. 准备会议链接 + 录屏权限
# 3. 发送邀请（用上方"会议邀请模板"）
# 4. 会议前 24h：发提醒 + 链接本文档
# 5. 会议中：主持 + 记录每项决策（用"决策记录模板"）
# 6. 会议后：填跟踪表 + 24h 内写 TaskRunner #10 评论
```

## 📋 关联文档

- [tadr-308 Phase 0 BLOCKING GATE UsrLinuxEmu Issue Draft](./tadr-308-phase0-blocking-usrlinuxemu-issue-draft.md) —— 会议前发出
- [tadr-308](../shared/adr/tadr-308-igpu-driver-vram-load.md) —— canonical design
- [实施路线图 R2](../../umd-evolution/roadmap/tadr-308-implementation-roadmap.md) —— 后续实施以此为准
- [UsrLinuxEmu ADR-090 v2 §D1](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/00_adr/adr-090-ptxir-via-h2d-dma-v2.md) —— HAL/ioctl 契约
- [PTX-EMU ADR-0029 §D8](https://github.com/chisuhua/PTX-EMU/blob/main/docs/adr/ADR-0029-ptxemu-image-executor.md#d8-cp-端集成集成--integrate-load-er) —— 待 amendment