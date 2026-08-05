---
SCOPE: TEST-FIXTURE
STATUS: PROPOSED
---

# TADR-114: REGISTER_GPU / CREATE_QUEUE / QUERY_QUEUE Semantic Alignment

**状态**: 🔄 Proposed
**日期**: 2026-08-04 (创建)
**提案人**: Chi Suhua
**评审者**: (待定)
**关联 ADR (UsrLinuxEmu)**: (无独立 canonical — 见 08-03 E2E 强化 commits)
**关联 Source**: include/shared/igpu_driver.hpp:register_gpu/create_queue/destroy_va_space;
UsrLinuxEmu plugins/gpu_driver/shared/gpu_ioctl.h (派发表 37→38)

---

## Context

UsrLinuxEmu 2026-08-03 强化派发表 (`85cfdcb`: REGISTER_GPU + MAP_QUEUE_RING E2E;
`709188c`: DESTROY_VA_SPACE invalidation + QUERY_QUEUE E2E semantics; `1ebf8f5`: ABI CI gate;
`541917d`: IOCTL handler count 37→38; `a9a0715`: MMU + firmware callback wire-up)。
TaskRunner IGpuDriver 有 `register_gpu` / `create_queue` / `destroy_queue` / `destroy_va_space`
但 **无 `query_queue`** → UsrLinuxEmu 加强的 QUERY_QUEUE 语义 TaskRunner 无法消费。
ABI CI gate 保证 ioctl 派发表一致,本 TADR 关注 IGpuDriver 表面层的语义对齐与缺口识别。

## Decision

1. **consumer-lens mirror** (tadr-110 模式): 记录现有方法语义对齐 + 新增需求登记,
   不定义具体方法签名 (实施时另建 TADR)。
2. **语义对齐表**:

| IGpuDriver 方法 | UsrLinuxEmu 强化点 (08-03) | TaskRunner 现状 |
|-----------------|----------------------------|------------------|
| `register_gpu` | E2E 覆盖 (`85cfdcb`) | ✅ 已有 inline forwarding,无需变更 |
| `create_queue` | (含于派发表) | ✅ 已有 inline forwarding |
| `destroy_queue` | (派生) | ✅ 已有 |
| `destroy_va_space` | invalidation 语义强化 (`709188c`) | ⚠️ 已有 inline forwarding,**需验证与新强化一致** |
| **`query_queue`** | **语义强化 (`709188c`)** | ❌ **缺失,需新增** |

3. **query_queue 需求登记**:
   - 应返回: 队列类型 / 状态 / 属性 (具体语义以 UsrLinuxEmu QUERY_QUEUE ioctl 为 canonical)
   - ioctl 编号: 需在实施 TADR 中确认 (08-03 强化范围内)
   - **实施时**: 新建独立 consumer-lens TADR (类似 TADR-110 模式),定义签名/派发/参数语义
4. **STATUS=PROPOSED**,实施路径:
   - 验证 `register_gpu` / `create_queue` / `destroy_va_space` 现有语义与 08-03 强化一致
   - 新增 `query_queue` 方法 → E2E 覆盖 → 通过测试后升 ACCEPTED

## Consumer-Lens

TaskRunner 观察 UsrLinuxEmu 派发表强化,识别 IGpuDriver 表面层是否需扩展。
本次识别出 1 个缺失 (`query_queue`),其余已有方法需语义一致性验证。

## Consequences

### 正面

- 识别新增需求 (`query_queue`),避免后续 API 缺失阻塞 UMD 实施
- 验证现有方法与上游强化一致,提前发现潜在语义漂移
- 与 tadr-301 (Stability Rules,允许新增方法) 一致

### 负面 / 风险

- `query_queue` 需 IGpuDriver 签名变更 (新增方法 = backward-compatible per tadr-301 Rule 2)
- `destroy_va_space` invalidation 语义变化可能导致现有 TaskRunner 行为差异
- 与 tadr-110 实施时序需协调 (都在 Phase 3.1/3.2 实施窗口内)

### 实施路径备注

- 本 TADR 验证 + 新增 `query_queue` 时,另建 TADR-XXX (consumer-lens, STATUS=PROPOSED)
- `query_queue` 命名与 UsrLinuxEmu QUERY_QUEUE ioctl 一致
- 验证现有方法语义时: 重跑 `mock_gpu_driver` 相关测试 + 增加对照 case

## 跨引用

- **tadr-301**: IGpuDriver 47 方法契约 (§Stability Rules,允许新增方法)
- **tadr-110**: IGpuDriver Phase 3.1/3.2 Real-Path consumer-lens (并行实施窗口)
- **tadr-111**: HAL L2 Foundation Removal consumer-lens (上游重构影响)
- **tadr-113**: Stage 4.4-4.6 ioctl Extension Tracking (本 TADR 与其 08-03 段重叠,本 TADR 聚焦
  register/queue 系列)
- **UsrLinuxEmu**: `85cfdcb` (REGISTER_GPU/MAP_QUEUE_RING E2E), `709188c` (DESTROY_VA_SPACE/
  QUERY_QUEUE semantics), `1ebf8f5` (ABI CI gate), `541917d` (handler count 37→38)
- **关联 Source**: `include/shared/igpu_driver.hpp`:register_gpu/create_queue/destroy_va_space;
  `plugins/gpu_driver/shared/gpu_ioctl.h` (派发表 37→38)

---

**最后更新**: 2026-08-04
