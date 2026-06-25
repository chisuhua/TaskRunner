# GitHub Issue 草稿: H-7 Issue #1 stream_id u32 类型不匹配

> **使用说明**: 本文件是 TaskRunner owner 准备贴到 UsrLinuxEmu 仓的 GitHub issue 草稿。
> **目标仓**: `github.com/chisuhua/UsrLinuxEmu`
> **目标标签**: `h7-deferred`、`phase3-prerequisite`、`abi-change`、`breaking-change-with-deprecation`
> **状态**: 📋 待 TaskRunner owner 提交

---

## Issue Title

```
H-7 Issue #1: stream_id u32 ABI 类型不匹配 — 提议拓宽到 u64 + 向后兼容方案
```

## Issue Body

```markdown
## 问题描述

ADR-034 §Issue #1 跟踪的 `stream_id` 类型不匹配问题。当前 ABI 字段 `gpu_pushbuffer_args.stream_id` 是 `__u32`，但内部维护的 `next_queue_handle_` 是 `uint64_t`，导致 `create_queue` 在 `UINT32_MAX` 附近失败。

### 触发位置

**`plugins/gpu_driver/shared/gpu_ioctl.h:43`** (ABI 结构体):
```cpp
struct gpu_pushbuffer_args {
    __u64 entries_addr;
    __u32 count;
    __u32 stream_id;              // ← 32-bit ABI 字段
    __u64 va_space_handle;
    __u64 reserved[2];
};
```

**`plugins/gpu_driver/drv/gpgpu_device.cpp:260-262`**:
```cpp
if (std::find(attached.begin(), attached.end(),
              static_cast<uint64_t>(args->stream_id)) == attached.end()) {  // ← R2 mapping 截断
```

**`plugins/gpu_driver/drv/gpgpu_device.h`** (内部变量):
```cpp
uint64_t next_queue_handle_ = 1;  // 内部 u64，但 ABI 限制为 u32
```

### 风险

1. **生产硬上限**: 每个进程最多创建 ~40 亿个 queue，长跑服务数月后必然触发
2. **R2 mapping 反模式**: AMD/NVIDIA 都用 u64 handle，TaskRunner 的 u32 截断是反模式
3. **下游耦合**: 所有通过 `/dev/gpgpu0` ioctl 提交 pushbuffer 的 caller 必须同步升级
4. **诊断困难**: `UINT32_MAX` 附近触发时，错误日志显示的 `stream_id` 是截断后的值，难定位 root cause

## 推荐方案

### PR 1: ABI 拓宽 + 向后兼容（最小化改动）

```diff
// gpu_ioctl.h:43
struct gpu_pushbuffer_args {
    __u64 entries_addr;
    __u32 count;
-   __u32 stream_id;              // ❌ 32-bit
+   __u64 stream_id;              // ✅ 64-bit
+   __u32 stream_id_compat;       // ⚠️ deprecated alias（旧调用方使用）
+   __u32 flags_extended;           // ✅ 新 flag 空间（reserved）
    __u64 va_space_handle;
    __u64 reserved[2];
};

// gpgpu_device.cpp:262
- uint64_t effective_id = static_cast<uint64_t>(args->stream_id);
+ uint64_t effective_id = args->stream_id;
+ if (effective_id == 0 && args->stream_id_compat != 0) {
+     // 向后兼容：旧调用方未设置 stream_id，但设置了 stream_id_compat
+     effective_id = static_cast<uint64_t>(args->stream_id_compat);
+ }
+ if (effective_id == 0 && args->stream_id_compat == 0) {
+     // 新调用方：0 是有效 u64 handle（实际 handle 从 1 开始分配，不冲突）
+     effective_id = 0;  // 保持现有 guard 行为
+ }
```

### PR 2: 废弃 alias 字段清理（6 月后，可选）

- 移除 `stream_id_compat` 字段
- 移除 `gpgpu_device.cpp` 中 backward compat fallback 逻辑
- 更新所有 caller（kernel module + userspace helper）

## 调研支撑

**TaskRunner owner 委托调研**（2026-06-26，详见 `docs/shared/research/gpu-queue-id-patterns-2026-06-26.md`）:

| 平台 | handle 类型 | 模式 | ABI 兼容策略 |
|------|------------|------|------------|
| AMD ROCm KFD | `HSAuint64` = `struct queue*` | 间接引用（指针） | 完整 u64 指针 |
| AMD ROCm HSA Runtime | `hsa_queue_t*` (64-bit pointer) | 间接引用 | 无 deprecated alias |
| NVIDIA CUDA | `CUstream` = `CUstream_st*` (typedef) | 不透明指针 | 无 deprecated alias |
| NVIDIA UVM | `uvm_va_block_region_t` (u64) | packed handle | major version bump |
| **TaskRunner 当前** | **`__u32` ABI + `u64` 内部** | **R2 mapping 截断** | **N/A (反模式)** |
| **推荐方案** | **`__u64` ABI + deprecated alias** | **完整 handle** | **deprecated alias + 6 月过渡期** |

**关键优势**：
- 与 AMD/NVIDIA 生态一致（u64 handle 是行业标准）
- 向后兼容（旧调用方传 `stream_id_compat` 仍工作）
- 6 月过渡期足够覆盖所有 caller 升级周期
- 不引入新依赖
- 生产硬上限从 ~40 亿提升到 2^64（理论无限）

## 关联资源

- **ADR-034** (H-7 Deferred Registry) §Issue #1：完整问题描述 + 修复路径
- **TaskRunner tadr-105**：[`external/TaskRunner/docs/test-fixture/adr/tadr-105-h7-deferred.md`](https://github.com/chisuhua/TaskRunner/blob/main/docs/test-fixture/adr/tadr-105-h7-deferred.md) — TaskRunner 侧 mirror
- **TaskRunner tadr-104 R2 mapping**：[`external/TaskRunner/docs/test-fixture/adr/tadr-104-r2-mapping.md`](https://github.com/chisuhua/TaskRunner/blob/main/docs/test-fixture/adr/tadr-104-r2-mapping.md) — 当前 LOW32 截断的 workaround 设计
- **openspec change**：[`openspec/changes/2026-06-26-h3-8-issue-1-coordination/`](https://github.com/chisuhua/UsrLinuxEmu/tree/main/openspec/changes/2026-06-26-h3-8-issue-1-coordination) — 协调工作追踪
- **AMD ROCm / NVIDIA CUDA 调研**：`docs/shared/research/gpu-queue-id-patterns-2026-06-26.md`（待创建）

## 影响范围

- **修改文件**: `gpu_ioctl.h` (1 处) + `gpgpu_device.cpp` (1 处) + `gpgpu_device.h` (文档化)
- **新增字段**: `stream_id_compat` + `flags_extended`（2 个字段）
- **不改变**: `GPU_IOCTL_*` ioctl 编号 / IGpuDriver 接口契约 / 其他 drv/sim/hal 代码
- **回归风险**: 低（deprecated alias 保证旧调用方兼容）
- **跨仓影响**: TaskRunner 仓 submodule bump + tadr-105 状态更新 + 下游 caller 通知
- **6 月后影响**: PR 2 移除 deprecated alias（需 caller 提前升级）

## 提议步骤

1. **评估**：UsrLinuxEmu owner 评估本提议（如拒绝请说明原因）
2. **实施 PR 1**：ABI 拓宽 + 向后兼容 fallback（建议 3-5 天）
   - 修改 `gpu_ioctl.h`：u32 → u64 + 新增字段
   - 修改 `gpgpu_device.cpp`：移除 `static_cast` + 增加 fallback 逻辑
   - 更新错误日志：使用 `effective_stream_id`（不再截断）
   - 测试：u64 边界值 + ABI 向后兼容 + 双驱动版本共存
3. **跨仓同步**：
   - UsrLinuxEmu 仓 PR merged
   - TaskRunner 仓 submodule bump
   - TaskRunner 仓 tadr-105 状态更新（Issue #1 → Accepted）
   - UsrLinuxEmu 仓 archive openspec change
4. **下游 caller 通知**：CHANGELOG.md + GitHub discussion（6 月过渡期）
5. **6 月后 PR 2**：废弃 alias 字段清理（独立时间线）

## 期望评估时间

建议 UsrLinuxEmu owner 在 **1 周内** 评估本提议，决定是否启动 PR 1。

## 关联人员

- **提议人**: TaskRunner owner (H-3.8 协调)
- **期望评审者**: UsrLinuxEmu owner
- **跨仓协调**: 按 ADR-035 §Rule 5.1 4 步流程

---

**TaskRunner owner 提交时检查清单**:
- [ ] Issue title 正确
- [ ] Labels: `h7-deferred`、`phase3-prerequisite`、`abi-change`、`breaking-change-with-deprecation`
- [ ] Assignees: UsrLinuxEmu owner (如知道)
- [ ] Milestone: `H-3.8` (如已建立)
- [ ] Project: `UsrLinuxEmu` 主项目
- [ ] Linked PRs: （待 PR 1 创建后链接）
```

---

## 提交清单

> **TaskRunner owner 准备贴到 UsrLinuxEmu 仓的 GitHub UI**:

| 字段 | 值 |
|------|---|
| **Title** | `H-7 Issue #1: stream_id u32 ABI 类型不匹配 — 提议拓宽到 u64 + 向后兼容方案` |
| **Body** | （上面 ```markdown ... ``` 块的内容）|
| **Labels** | `h7-deferred`, `phase3-prerequisite`, `abi-change`, `breaking-change-with-deprecation` |
| **Assignees** | UsrLinuxEmu owner |
| **Milestone** | H-3.8 (待建立) |
| **Project** | UsrLinuxEmu |

## 提交后跟踪

- [ ] 在 TaskRunner 仓 `docs/test-fixture/adr/tadr-105-h7-deferred.md` 添加 §H-3.8 GitHub Issue Reference 段
- [ ] 在 openspec change `tasks.md` Phase B 标记 B.1 完成
- [ ] 通知 UsrLinuxEmu owner（Slack / Email / 其他）