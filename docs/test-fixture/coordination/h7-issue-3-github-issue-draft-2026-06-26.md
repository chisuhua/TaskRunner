# GitHub Issue 草稿: H-7 Issue #3 attached_queues 弱校验

> **使用说明**：本文件是 TaskRunner owner 准备贴到 UsrLinuxEmu 仓的 GitHub issue 草稿。
> **目标仓**：`github.com/chisuhua/UsrLinuxEmu`
> **目标标签**：`h7-deferred`、`phase3-prerequisite`、`good-first-issue`
> **状态**：📋 待 TaskRunner owner 提交

---

## Issue Title

```
H-7 Issue #3: attached_queues 弱校验 (linear std::find) — 提议升级为 O(1) 数据结构
```

## Issue Body

```markdown
## 问题描述

ADR-034 §Issue #3 跟踪的 `attached_queues` 弱校验问题。当前实现使用 `std::vector<uint64_t>` + `std::find` 做 O(n) 线性查找，**无** lifecycle/type/binding 断言。

### 触发位置

**`plugins/gpu_driver/drv/gpgpu_device.h:77`**：
```cpp
struct VASpace {
    uint64_t handle;
    uint32_t page_size;
    uint32_t flags;
    uint64_t created_at;
    std::vector<uint64_t> attached_queues;  // ← 弱校验根源
};
```

**`plugins/gpu_driver/drv/gpgpu_device.cpp:260-262`**：
```cpp
const auto& attached = va_spaces_[args->va_space_handle].attached_queues;
if (std::find(attached.begin(), attached.end(),
              static_cast<uint64_t>(args->stream_id)) == attached.end()) {
    usr_linux_emu::Logger::warn(
        "[GpgpuDevice] PUSHBUFFER_SUBMIT_BATCH: stream_id " +
        std::to_string(args->stream_id) + " not attached...");
    return -EINVAL;
}
```

### 风险

1. **性能问题**：O(n) 线性查找，n = 关联 queue 数量
2. **弱校验**：仅存在性检查，**无** lifecycle/type/binding 断言
3. **静默错误**：返回 `-EINVAL` 无类型区分，难以诊断 root cause
4. **race condition**：`destroy_va_space` 后仍可能 `submit_batch` 接受已 destroy 的 queue

## 推荐方案

**最小化改动**（PR 1 范围）：
```diff
 // gpgpu_device.h:77
 struct VASpace {
     ...
-    std::vector<uint64_t> attached_queues;  // O(n) std::find
+    std::unordered_set<uint64_t> attached_queues;  // O(1) avg
 };

 // gpgpu_device.cpp:261
-    if (std::find(attached.begin(), attached.end(),
-                  static_cast<uint64_t>(args->stream_id)) == attached.end()) {
+    if (attached.find(static_cast<uint64_t>(args->stream_id)) == attached.end()) {
```

**附加改进**（PR 2 范围，可选）：
- 错误码语义化：`-EINVAL` / `-ENOENT` / `-EBUSY` 区分
- lifecycle_state 字段
- race condition atomic check-and-set

## 调研支撑

**TaskRunner owner 委托调研**（2026-06-26）：

| 平台 | 数据结构 | 性能 | 备注 |
|------|---------|------|------|
| AMD ROCm KFD | 链表 (O(n)) | n ≤ 127 | 因 cap 上限小 |
| AMD ROCm UMQ | 红黑树 | O(log n) | 多 GPU 场景 |
| NVIDIA UVM | 红黑树 | O(log n) | N 通常 > 1000 |
| NVIDIA libcuda | 数组 + bitmap | O(1) | 固定 cap |
| **推荐** | **`std::unordered_set`** | **O(1) avg** | C++ 标准库最低改动 |

**关键优势**：
- 1 行 include + 1 行代码改动（最低风险）
- 行为完全兼容（`find` 返回 iterator / end，与 `std::find` 语义一致）
- O(1) avg 性能满足 N 通常 < 100 的场景
- 不引入新依赖

## 关联资源

- **ADR-034** (H-7 Deferred Registry) §Issue #3：完整问题描述 + 修复路径
- **TaskRunner tadr-105**：[`external/TaskRunner/docs/test-fixture/adr/tadr-105-h7-deferred.md`](https://github.com/chisuhua/TaskRunner/blob/main/docs/test-fixture/adr/tadr-105-h7-deferred.md) — TaskRunner 侧 mirror
- **openspec change**：[`openspec/changes/2026-06-26-h3-6-issue-3-coordination/`](https://github.com/chisuhua/UsrLinuxEmu/tree/main/openspec/changes/2026-06-26-h3-6-issue-3-coordination) — 协调工作追踪
- **AMD ROCm / NVIDIA CUDA 调研**：见 openspec change `design.md` §调研支撑

## 影响范围

- **修改文件**：`gpgpu_device.h` (1 处) + `gpgpu_device.cpp` (1 处) + `<unordered_set>` include
- **不修改**：`gpu_ioctl.h` ABI / IGpuDriver 接口契约 / 其他 drv/sim/hal 代码
- **回归风险**：低（行为完全兼容）
- **跨仓影响**：TaskRunner 仓 submodule bump + tadr-105 状态更新

## 提议步骤

1. **评估**：UsrLinuxEmu owner 评估本提议（如拒绝请说明原因）
2. **实施 PR 1**：最小化 `unordered_set` 改动（建议 1-2 天）
3. **跨仓同步**：
   - UsrLinuxEmu 仓 PR merged
   - TaskRunner 仓 submodule bump
   - TaskRunner 仓 tadr-105 状态更新（Issue #3 → Accepted）
   - UsrLinuxEmu 仓 archive openspec change
4. **可选 PR 2**：错误码语义化（如评估后启动，独立 PR）

## 期望评估时间

建议 UsrLinuxEmu owner 在 **1 周内**评估本提议，决定是否启动 PR 1。

## 关联人员

- **提议人**：TaskRunner owner (H-3.6 协调)
- **期望评审者**：UsrLinuxEmu owner
- **跨仓协调**：按 ADR-035 §Rule 5.1 4 步流程

---

**TaskRunner owner 提交时检查清单**：
- [ ] Issue title 正确
- [ ] Labels: `h7-deferred`、`phase3-prerequisite`、`good-first-issue`
- [ ] Assignees: UsrLinuxEmu owner (如知道)
- [ ] Milestone: `H-3.6` (如已建立)
- [ ] Project: `UsrLinuxEmu` 主项目
- [ ] Linked PRs: （待 PR 1 创建后链接）
```

---

## 提交清单

> **TaskRunner owner 准备贴到 UsrLinuxEmu 仓的 GitHub UI**：

| 字段 | 值 |
|------|---|
| **Title** | `H-7 Issue #3: attached_queues 弱校验 (linear std::find) — 提议升级为 O(1) 数据结构` |
| **Body** | （上面 ```markdown ... ``` 块的内容）|
| **Labels** | `h7-deferred`, `phase3-prerequisite`, `good-first-issue` |
| **Assignees** | UsrLinuxEmu owner |
| **Milestone** | H-3.6 (待建立) |
| **Project** | UsrLinuxEmu |

## 提交后跟踪

- [ ] 在 TaskRunner 仓 `docs/test-fixture/adr/tadr-105-h7-deferred.md` 添加 §H-3.6 GitHub Issue Reference 段
- [ ] 在 openspec change `tasks.md` Phase B 标记 B.1 完成
- [ ] 通知 UsrLinuxEmu owner（Slack / Email / 其他）
