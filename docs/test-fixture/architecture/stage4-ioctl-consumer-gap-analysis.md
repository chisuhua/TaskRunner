---
SCOPE: TEST-FIXTURE
STATUS: PROPOSED
DATE: 2026-08-04
RELATED:
  - tadr-113 (Stage 4.4-4.6 ioctl Extension Tracking)
  - tadr-301 (IGpuDriver 47 方法契约)
  - tadr-302 (Sync Primitives 抽象)
---

# 架构差距分析: stage4-ioctl-consumer

> **生成日期**: 2026-08-04
> **状态**: 草案 (PROPOSED)
> **关联 ADR**: tadr-113 (test-fixture); UsrLinuxEmu ADR-047, ADR-049, ADR-051, ADR-052, ADR-046

## 1. 目标架构

TaskRunner IGpuDriver 暴露所有上游 Stage 4 ioctl 能力 (consumer-lens 完整覆盖):

```
┌─────────────────────────────────────────────────────────┐
│  umd-evolution shim (待扩展,见 G3-G7)
│  ↓ 通过 IGpuDriver 调用
├─────────────────────────────────────────────────────────┤
│  IGpuDriver (含 Phase 3.1/3.2 + Stage 4.4-4.6 完整方法集)
│  ↓
├─────────────────────────────────────────────────────────┤
│  GpuDriverClient (inline ioctl forwarding,0x50-0x80+ range)
├─────────────────────────────────────────────────────────┤
│  UsrLinuxEmu sim (Stage 4.4-4.6 全部已实现 ✅)
└─────────────────────────────────────────────────────────┘
```

**验收标准:**
- IGpuDriver 完整覆盖 Stage 4.4-4.6 ioctl 能力 (SEM/IB_JUMP/Predicate/AQL/PDL/ContextType/Timeline)
- 每类能力对应 1 个 consumer-lens 实施 TADR (PROPOSED)
- 触发消费条件出现时,新建 TADR + 实施 → 测试 → 升 ACCEPTED

## 2. 当前架构

| 类别 | Stage | UsrLinuxEmu 状态 | TaskRunner IGpuDriver | TaskRunner 消费窗口 |
|------|-------|------------------|------------------------|---------------------|
| Semaphore | 4.4 (07-28) | ✅ SEM_WAIT/RELEASE/BARRIER opcodes | ❌ 无对应方法 | Phase 1.5+ / fence 高级用例 |
| IB JUMP | 4.4 (07-28) | ✅ IB_JUMP opcode + gpu_ib_ref | ❌ 无对应方法 | 与 Phase 3.1 graph 实施联合 |
| Predicate | 4.5 (07-31) | ✅ GPU_OP_SET_PREDICATE + applyPredicateOp | ❌ 无对应方法 | Phase 3+ graph 依赖条件 |
| AQL | 4.5 (07-31) | ✅ FORMAT_AQL/PM4 + AQL packet parser | ❌ 无对应方法 | Phase 4+ UMD shim 启用 |
| ContextType | 4.6 (08-01) | ✅ ContextType + MQD.context_type + GREEN/BROWN | ❌ 无对应方法 | GREEN Context 启用时 |
| PDL | 4.6 (08-01) | ✅ GPU_OP_PDL_LAUNCH + pdl_nest + hal_pdl_* | ❌ 无对应方法 | 与 graph 依赖同步,Phase 3+ |
| Timeline | 4.5 (07-29) | ✅ gpfifo_entry.timeline + SemaphoreManager | ❌ 无对应方法 (wait_fence 已 u64 fence_id) | Phase 1.5+ |

**关键事实:**
- TaskRunner IGpuDriver 维持 47 方法 (tadr-301),对 Stage 4.4-4.6 7 类能力**完全未暴露**
- UsrLinuxEmu 端 ioctl 已实现,只是 TaskRunner 表面层未消费
- 7 类能力中 Semaphore/IB_JUMP/PDL 与 graph/同步/fence 路径强相关

## 3. 差距清单

| # | 差距项 | 严重程度 | 优先级 | 关联 |
|---|--------|---------|--------|------|
| G1 | Semaphore (SEM_WAIT/SEM_RELEASE/BARRIER) IGpuDriver 方法缺失 | 🟠 中 | P1 | tadr-113 row 1 |
| G2 | IB_JUMP + gpu_ib_ref IGpuDriver 方法缺失 (与 graph 节点跳转相关) | 🟠 中 | P1 | tadr-113 row 2 |
| G3 | SET_PREDICATE + applyPredicateOp (PredicateState) 方法缺失 | 🟡 中 | P2 | tadr-113 row 3 |
| G4 | FORMAT_AQL/PM4 + AQL packet parser 路径未消费 (HSA AQL) | 🟡 低 | P2 | tadr-113 row 4 |
| G5 | ContextType + MQD.context_type + GREEN/BROWN 调度方法缺失 | 🟡 中 | P2 | tadr-113 row 5 |
| G6 | PDL (GPU_OP_PDL_LAUNCH + pdl_nest + hal_pdl_*) 方法缺失 (graph 依赖模型) | 🟠 中 | P1 | tadr-113 row 6 |
| G7 | gpfifo_entry.timeline + SemaphoreManager 桥接方法缺失 (wait_fence 扩展) | 🟡 中 | P1 | tadr-113 row 7 |

## 4. 补齐路径

**实施准则:** 每类别消费时新建独立 consumer-lens TADR (类似 TADR-110 模式),遵循统一流程:
```
新建 TADR-XXX (consumer-lens, STATUS=PROPOSED)
  → 实现 IGpuDriver 方法 + GpuDriverClient forwarding
  → E2E 测试 + sanitizer 覆盖
  → 验证通过后升 ACCEPTED
```

**触发消费的条件 (任一类别出现):**
- 新 API 引入该能力 (如 `cuStreamWaitValue32` / `cuLaunchKernelEx` with attribute)
- UMD-EVOLUTION → ACCEPTED promote 中要求该能力
- test-fixture 测试用例提出该能力需求

**优先级实施建议:**
1. **G6 PDL + G2 IB_JUMP**: 与 Phase 3.1 graph 实施联合 (TADR-110),自然同步
2. **G1 Semaphore + G7 Timeline**: 与 Phase 1.5 fence_id 高级用例联合 (wait_fence 语义扩展)
3. **G3 Predicate**: graph 节点依赖执行,与 graph 实施联合
4. **G5 ContextType + G4 AQL**: GREEN Context / HSA AQL 启用时

**依赖顺序:** 各自独立,可并行;但与 G2/IB_JUMP 与 Phase 3.1 graph (TADR-110) 共享 ioctl 范围,需协调 ioctl 编号分配

## 5. 参考资料

- **本仓 TADR**: tadr-113 (登记 + 消费窗口); tadr-301 (IGpuDriver 契约); tadr-302 (Sync Primitives)
- **本仓架构文档**: docs/test-fixture/architecture/{capabilities,current,layers}.md
- **UsrLinuxEmu ADRs**:
  - ADR-047 (Hardware Semaphore Barrier): `915a8c9` SemaphoreManager bridge
  - ADR-049 (Cross-Engine Synchronization,含 D1 waiter): `4bc616e` Accepted; `6739698` 3-level priority + starvation
  - ADR-051 (Predication/Conditional Execution): `14afcdb` SET_PREDICATE; `71c73aa` applyPredicateOp
  - ADR-052 (AQL/PM4 Native Support): `71c73aa` AQL packet parser + format dispatch
  - ADR-046 (Preemption/Context Switch): `5939080` preemption-timeline-sem
- **UsrLinuxEmu commits**:
  - `0c194ea` SEM_WAIT/SEM_RELEASE/BARRIER/IB_JUMP opcodes
  - `800fb59` gpu_ib_ref IB JUMP
  - `24de3b7` gpfifo_entry.timeline + Puller semaphore
  - `8b6085a` GPU_OP_PDL_LAUNCH + gpu_pdl_payload + MAX_PDL_NEST
  - `a1d3d4a` hal_green_context_create/destroy
  - `38bd8a9` gpu_queue_args.context_type
  - `f1c9c24` ContextType enum + MQD.context_type
- **架构 SSOT**: UsrLinuxEmu `docs/02_architecture/post-refactor-architecture.md`