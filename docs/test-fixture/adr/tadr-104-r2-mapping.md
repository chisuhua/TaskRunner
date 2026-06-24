---
SCOPE: TEST-FIXTURE
STATUS: ACCEPTED
REPLACES: tadr-007
---

# TADR-007: R2 Mapping Contract — LOW32 Truncation 显式化

**状态**: ✅ Accepted
**日期**: 2026-06-23
**提案人**: TaskRunner owner (H-3 review 阶段识别)
**评审者**: UsrLinuxEmu Architecture Team + TaskRunner owner
**关联 ADR (UsrLinuxEmu)**: [ADR-033 §R2 Mapping Contract](../../../../docs/00_adr/adr-033-h3-phase2-lifecycle.md) (canonical)
**关联 Change**: `openspec/changes/archive/2026-06-22-h3-phase2-management/`
**关联 Source**: H-3 design.md §R2

---

## Context

`create_queue()` 返回的 `uint64_t queue_handle` 在 submit 时需转 `uint32_t stream_id`（LOW32 取低 32 位）。上游决策由 UsrLinuxEmu ADR-033 §R2 记录。

本 TADR 记录 **TaskRunner 侧 CLI 实施的显式化策略**，避免 implementation-defined narrowing。

## Decision

在 CLI `cuda_queue create` 输出中**显式**打印 LOW32 truncation，让 R2 mapping 透明：

```cpp
// src/cmd_cuda.cpp:349-352
std::cout << "queue_handle=" << queue_handle << "\n";
// R2 mapping: caller must save full u64 handle; submit_batch uses LOW32 as stream_id
// (explicit & 0xFFFFFFFFULL to make truncation obvious, matches gpgpu_device.cpp:262)
std::cout << "  (R2 mapping: stream_id=" << static_cast<uint32_t>(queue_handle & 0xFFFFFFFFULL)
          << " = LOW32(queue_handle))\n";
```

**关键点**：
1. 使用 `static_cast<uint32_t>(queue_handle & 0xFFFFFFFFULL)` 而非隐式截断
2. CLI 输出同时打印完整 `queue_handle`（u64）和 `stream_id`（u32 LOW32）
3. 注释显式引用 UsrLinuxEmu 校验侧 `gpgpu_device.cpp:262`

## Consumer-Lens

### 实施 commit

```
8625b82 refactor(cli): make R2 mapping truncation explicit in cuda_queue (H-3 review)
```

### 显式化的价值

- **可读性**：CLI 用户立即看到 `stream_id = LOW32(queue_handle)` 关系
- **可追溯性**：注释引用 UsrLinuxEmu 校验位置，跨仓可对应
- **避免 narrowing warning**：显式 `& 0xFFFFFFFFULL` 消除 `-Wnarrowing` 警告
- **避免 implementation-defined 行为**：C++ 隐式 u64→u32 截断在某些平台 implementation-defined

### UsrLinuxEmu 侧对应位置

```cpp
// UsrLinuxEmu/plugins/gpu_driver/drv/gpgpu_device.cpp:260-262
const auto& attached = va_spaces_[args->va_space_handle].attached_queues;
if (attached.find(static_cast<uint64_t>(args->stream_id)) == attached.end()) {
    return -EINVAL;  // stream_id 零扩展后必须在 attached_queues 中
}
```

零扩展（`static_cast<uint64_t>`）+ LOW32 截断（TaskRunner CLI）形成完整 mapping 契约。

## Consequences

### 正面

- ✅ R2 mapping 透明（CLI 输出可见）
- ✅ 避免 implementation-defined narrowing
- ✅ 跨仓可追溯（注释引用 UsrLinuxEmu 校验侧）

### 负面 / 风险

- ⚠️ 仍受 ADR-034 Issue #1 约束：`next_queue_handle_` 超过 `UINT32_MAX` 时后续 create_queue 失败（TADR-008 推迟跟踪）

## 跨引用

- **上游 UsrLinuxEmu ADR-033**: §R2 Mapping Contract
- **关联 TADR**: TADR-006 (H-3 Phase 2 lifecycle), TADR-008 (H-7 Issue #1)
- **关联文件**: `src/cmd_cuda.cpp:351`

---

**最后更新**: 2026-06-23（H-4.5 docs governance cleanup 建立 TADR-007 R2 mapping consumer-lens）
