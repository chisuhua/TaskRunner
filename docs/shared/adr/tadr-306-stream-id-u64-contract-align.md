---
SCOPE: SHARED
STATUS: PROPOSED
---

# TADR-306: stream_id u32 ↔ u64 Cross-Repo Drift

**状态**: 🔄 Proposed (记录性 TADR,待 dual-ack)
**日期**: 2026-08-04 (创建)
**提案人**: Chi Suhua
**评审者**: (待定,需 dual-ack per shared/README.md)
**关联 ADR (UsrLinuxEmu)**: (无独立 canonical — 见 `02ae421` widening commit)
**关联 Source**: include/shared/igpu_driver.hpp (submit_*/stream_capture_*/submit_graph stream_id 参数);
UsrLinuxEmu plugins/gpu_driver/shared/gpu_types.h (stream_id 字段)

---

## Context

> **前身**: 本 TADR 由原 TADR-112 迁移而来 (2026-08-04 序号重整)。
> 引用 `tadr-112` 的旧文档应改读本文件。

UsrLinuxEmu 2026-06-26 (commit `02ae421`) 实施 **stream_id u32→u64 扩展**,提供 backward-compat fallback。
TaskRunner IGpuDriver 维持 `uint32_t stream_id` (`submit_batch`/`submit_memcpy`/`submit_launch`/
`stream_capture_*`/`submit_graph` 全部),最晚新增方法 `submit_graph` (commit `21f71c9`, 07-06)
仍使用 `uint32_t`,显示有意识延后。已采用 `uint64_t` 的 ID 类标识:`graph_handle` /
`graph_exec_handle` / `fence_id` / `pool_handle` / `bo_handle` / `va_space_handle` / `va_out`。

**本 TADR 属于 shared scope**: `stream_id` 是 IGpuDriver 共享契约的一部分,涉及跨仓契约决策,
按 docs/shared/README.md §"Review Requirements (Dual Approval)" 需 test-fixture + umd-evolution
双 maintainer 审查。

## Decision

1. **consumer-lens mirror**: 记录当前 ABI 漂移状态 + 触发再对齐条件,不重复上游决策文本。
2. **漂移分类表**:

| 侧 | 类型 | 备注 |
|----|------|------|
| UsrLinuxEmu `stream_id` | `uint64_t` (widened 06-26) | 提供 u32 backward-compat fallback |
| TaskRunner `IGpuDriver::submit_*` `stream_id` | `uint32_t` | 5 个方法全 u32 |
| TaskRunner `IGpuDriver::stream_capture_*` `stream_id` | `uint32_t` | 3 个方法全 u32 |
| TaskRunner `IGpuDriver::submit_graph` `stream_id` | `uint32_t` | 末次扩展 21f71c9 仍 u32 |

3. **触发再对齐条件** (任一出现即重审本 TADR):
   - **UMD-EVOLUTION → ACCEPTED promote** 中出现 u64 stream_id 实际需求 (如 high-density stream pool)
   - TaskRunner 内部出现 > 4G stream ID 使用场景
   - UsrLinuxEmu 端 **移除 backward-compat fallback** (即弃用 u32 路径)
4. **与 tadr-301 关系**: 本漂移视为 tadr-301 §Stability Rules 的已知 exception,需在 tadr-301
   中交叉引用("stream_id parameter is uint32_t; see tadr-306 for cross-repo drift status")。
5. 当前 STATUS=PROPOSED (记录性 TADR,待 dual-ack 后升 ACCEPTED)。

## Consumer-Lens

TaskRunner 不改 IGpuDriver 签名;UsrLinuxEmu fallback 维持 ABI 兼容;stream ID 仍视为 32-bit
标识在 TaskRunner 内部传递,跨仓边界由 UsrLinuxEmu 端负责兼容性转换。

## Consequences

### 正面

- 明确漂移状态,避免后续误改或被遗忘
- 触发条件清晰,避免无端重构破坏 ABI 稳定性
- 与 tadr-301 形成 exception 关系,文档链完整

### 负面 / 风险

- 漂移持续存在,跨仓心智负担增加
- backward-compat fallback 增加 UsrLinuxEmu 维护复杂度
- 若未来 stream ID 超过 u32 表达范围,TaskRunner 需紧急对齐 (提前预警)

### 实施路径备注

- 本 TADR 无实现动作,纯记录性
- 触发任一条件时,需新建实施类 TADR (PROPOSED) 处理对齐工作
- 跨 scope 联动: tadr-110 (Phase 3.1/3.2 consumer-lens) 引用本漂移,verify-graph 实施时需关注

## 跨引用

- **tadr-301**: IGpuDriver 47 方法契约 (§Stability Rules,本漂移为已知 exception)
- **tadr-305**: `mem_pool_export_shareable` (0x68)
- **tadr-110**: IGpuDriver Phase 3.1/3.2 Real-Path consumer-lens (test-fixture)
- **tadr-111**: HAL L2 Foundation Removal consumer-lens (test-fixture)
- **UsrLinuxEmu**: `02ae421` (stream_id u64 widening with backward-compat fallback)
- **关联 Source**: `include/shared/igpu_driver.hpp` (submit_*/stream_capture_*/submit_graph stream_id 参数);
  `plugins/gpu_driver/shared/gpu_types.h` (stream_id 字段定义)

---

**最后更新**: 2026-08-04