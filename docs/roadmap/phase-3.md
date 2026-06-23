# Phase 3 - Multi-GPU / P2P (Deferred)

> **状态**: Deferred (待 H-7 ADR 完成)
> **关联**: [TADR-008 H-7 deferred mirror](../adr/tadr-008-h7-deferred-mirror.md), UsrLinuxEmu [ADR-034](../../../../docs/00_adr/adr-034-h7-deferred-registry.md)

## 为什么 Deferred

Phase 3 需要先解决 H-7 ADR 注册的 3 个 UsrLinuxEmu 上游 issues：

1. **Issue #1**: stream_id u32 与 queue_handle u64 类型不匹配 (UINT32_MAX 限制)
2. **Issue #2**: ioctl path 绕过 GpuQueueEmu 抽象层 (行为分歧风险)
3. **Issue #3**: attached_queues 弱校验 (静默 -EINVAL 难诊断)

详见 [TADR-008 §3 issues 与 TaskRunner 侧影响](../adr/tadr-008-h7-deferred-mirror.md#decision)。

## Phase 3 目标（待 H-7 触发）

### 1. Multi-GPU 支持

- 多个 `GpuDriverClient` 实例（每个对应一个 `/dev/gpgpu<N>` 设备节点）
- `register_gpu()` 在多个 GPU 之间分配 workload
- Cross-GPU memory sharing

### 2. P2P (Peer-to-Peer) 支持

- `cudaMemcpyPeer` / `cudaMemcpyPeerAsync` 等价实现
- 跨 GPU 直接数据传输（不走 host memory staging）
- P2P topology discovery

### 3. 多 GPU 场景下的 VA Space 共享

- 单个 VA Space 包含多个 GPU 的地址空间
- 跨 GPU 指针传递（handle 翻译）

## 验收标准（待 H-7 完成后定义）

- [ ] Multi-GPU `cudaSetDevice` / `cudaGetDevice` 路径
- [ ] `cudaMemcpyPeer` 跨 GPU 验证
- [ ] P2P topology discovery 与使能
- [ ] Issue #1 / #2 / #3 修复并验证

## 关键决策（前置 TADR）

- **TADR-005** (IGpuDriver 抽象层) - Multi-GPU 需要为每个 GPU 独立 `IGpuDriver*`
- **TADR-006** (Phase 2 5 方法) - `register_gpu` 已预留 multi-GPU 入口
- **TADR-007** (R2 mapping) - `submit_batch` 用 LOW32, Multi-GPU 需重新评估
- **TADR-008** (H-7 deferred mirror) - Issue #1/#2/#3 是前置硬条件

## 触发条件

任一以下事件触发 Phase 3 启动：

1. **Issue #1 修复** (H-7 ADR): `gpu_pushbuffer_args.stream_id` 拓宽为 `uint64_t`
2. **Issue #2 修复** (H-7 ADR): `GpuQueueEmu` 抽象层定义 + ioctl path / mmap path 统一
3. **Issue #3 修复** (H-7 ADR): `attached_queues` 元素含 `lifecycle_state` + 错误码语义改进
4. **新需求**: 第三方应用需要 Multi-GPU 支持

## 当前实施建议

**暂不开始 Phase 3 实施**。先推动 H-7 ADR 在 UsrLinuxEmu owner 端完成 (`openspec/changes/h7-adr/`)，TaskRunner 这边只需消费 H-7 修复成果。

---

**最后更新**: 2026-06-23 (H-4.5 docs governance cleanup 整理 phase 文档)
