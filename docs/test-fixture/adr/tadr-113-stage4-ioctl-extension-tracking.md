---
SCOPE: TEST-FIXTURE
STATUS: PROPOSED
---

# TADR-113: Stage 4.4-4.6 ioctl Extension Tracking (TaskRunner Consumption)

**状态**: 🔄 Proposed (追踪性 TADR)
**日期**: 2026-08-04 (创建)
**提案人**: Chi Suhua
**评审者**: (待定)
**关联 ADR (UsrLinuxEmu)**: ADR-047 (Hardware Semaphore Barrier), ADR-049 (Cross-Engine Synchronization,含 D1 waiter), ADR-051 (Predication/Conditional Execution), ADR-052 (AQL/PM4 Native Support), ADR-046 (Preemption/Context Switch)
**关联 Source**: UsrLinuxEmu `plugins/gpu_driver/shared/gpu_ioctl.h` (新增 opcode 段);
include/shared/igpu_driver.hpp (确认无对应方法)

---

## Context

UsrLinuxEmu Stage 4.4-4.6 (2026-07-28 ~ 08-01) 新增 5+ 类 ioctl 能力,TaskRunner IGpuDriver
**无任何对应方法** → 全部"上游已有 / 下游未消费"状态。本 TADR 登记这些 ioctl 能力的
TaskRunner 消费决策窗口,不做详细契约定义。

## Decision

1. **追踪性 TADR** (与 TADR-110/111 consumer-lens 区别): 仅登记 + 消费窗口决策,不定义
   方法签名/参数语义/错误码。实施时新建独立 consumer-lens TADR。
2. **类别登记表**:

| 类别 | Stage | 关键 ioctl/常量/feature | TaskRunner 关联 | 消费窗口 |
|------|-------|-------------------------|-----------------|----------|
| Semaphore | 4.4 | `SEM_WAIT`/`SEM_RELEASE`/`BARRIER` | 与 `submit_batch`/fence 同步相关 | Phase 1.5+ / fence 高级用例 |
| IB JUMP | 4.4 | `IB_JUMP` opcode + `gpu_ib_ref` | 与 graph 节点跳转相关 | 与 Phase 3.1 graph 实施联合 |
| Predicate | 4.5 | `GPU_OP_SET_PREDICATE` + applyPredicateOp SET/AND/OR/XOR | 与 graph 节点依赖执行 | Phase 3+ graph 依赖条件 |
| AQL | 4.5 | `FORMAT_AQL`/`FORMAT_PM4` + AQL packet parser | HSA AQL 队列路径 | Phase 4+ UMD shim 启用 |
| ContextType | 4.6 | `ContextType` enum + `MQD.context_type` + GREEN/BROWN | 与 GREEN Context 调度相关 | GREEN Context 启用时 |
| PDL | 4.6 | `GPU_OP_PDL_LAUNCH` + `pdl_nest_counter` + `hal_pdl_*` | 依赖启动 (= graph 依赖模型) | 与 graph 依赖同步,Phase 3+ |
| Timeline | 4.5 | `gpfifo_entry.timeline` + `SemaphoreManager` | `wait_fence` 已 `uint64_t fence_id`,可能需扩展 | Phase 1.5+ |

3. **触发消费的条件** (任一类别出现即新建实施 TADR):
   - 新 API 引入该能力 (如 `cuStreamWaitValue32` / `cuLaunchKernelEx` with attribute)
   - UMD-EVOLUTION → ACCEPTED promote 中要求该能力
   - test-fixture 测试用例提出该能力需求
4. **STATUS=PROPOSED**,无实现动作,跟踪状态变化(上游能力新增/消费实施完成时升 ACCEPTED)。

## Consumer-Lens

本 TADR 是"上游能力 ↔ 下游待消费"的消费者视角登记。TaskRunner 当前 IGpuDriver 不暴露
这些 ioctl 的对应方法;消费时新建 consumer-lens TADR (类似 TADR-110),在那里定义方法
签名/ioctl 派发/参数语义。

## Consequences

### 正面

- 单一事实来源,避免消费决策分散在多个 issue/PR 中
- 上游能力分类登记,便于 TaskRunner 规划时检索
- 触发条件明确,避免无端实施或遗漏

### 负面 / 风险

- 跨 Stage 范围较大 (4.4-4.6),维护负担随上游演进增加
- 不含详细契约,消费时仍需另建 TADR
- 上游持续新增能力时需同步更新本表

### 实施路径备注

- 本 TADR 无实现动作,纯追踪记录
- 每类别消费时: 新建 TADR-XXX (consumer-lens, STATUS=PROPOSED) → 实现 → 验证 → 升 ACCEPTED
- 上游新增 Stage 时: 在本表追加新类别 + ADR 引用

## 跨引用

- **tadr-110**: IGpuDriver Phase 3.1/3.2 Real-Path consumer-lens (接口映射)
- **tadr-111**: HAL L2 Foundation Removal consumer-lens (上游重构影响)
- **tadr-306**: stream_id u32↔u64 Cross-Repo Drift (记录性, shared scope)
- **tadr-301**: IGpuDriver 47 方法契约 (shared)
- **tadr-302**: Sync Primitives 抽象 (Semaphore/Timeline 相关)
- **UsrLinuxEmu ADRs**: ADR-047, ADR-049, ADR-051, ADR-052, ADR-046
- **UsrLinuxEmu gpu_ioctl.h**: Stage 4.4-4.6 新增 opcode 段
- **关联 Source**: `include/shared/igpu_driver.hpp` (确认无对应方法)

---

**最后更新**: 2026-08-04
