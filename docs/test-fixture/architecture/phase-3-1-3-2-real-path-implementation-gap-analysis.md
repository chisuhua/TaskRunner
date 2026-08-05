---
SCOPE: TEST-FIXTURE
STATUS: PROPOSED
DATE: 2026-08-04
RELATED:
  - tadr-110 (Phase 3.1/3.2 Real-Path consumer-lens)
  - tadr-111 (HAL L2 Removal consumer-lens)
  - tadr-114 (REGISTER_GPU/QUEUE/QUERY semantic alignment)
---

# 架构差距分析: phase-3-1-3-2-real-path-implementation

> **生成日期**: 2026-08-04
> **状态**: 草案 (PROPOSED)
> **关联 ADR**: tadr-110, tadr-111, tadr-114 (test-fixture); tadr-301, tadr-305 (shared)

## 1. 目标架构

TaskRunner 侧 Phase 3.1/3.2 完整闭环 (consumer-lens):

```
┌─────────────────────────────────────────────────────────┐
│  umd-evolution shim (cu_graph*.cpp / cu_stream_capture.cpp / cu_mem_pool.cpp)
│  ↓ 通过 IGpuDriver 调用
├─────────────────────────────────────────────────────────┤
│  IGpuDriver 47 方法 (含 stream_capture×3 + graph×7 + mem_pool×6)
│  ↓
├─────────────────────────────────────────────────────────┤
│  GpuDriverClient (48 个 inline ioctl forwarding override,PR #7) ✅
│  ↓ ioctl 派发 0x50-0x68
├─────────────────────────────────────────────────────────┤
│  CudaStub (16 个 Phase 3.1/3.2 stub 仿真) ✅ (待补齐)
├─────────────────────────────────────────────────────────┤
│  UsrLinuxEmu sim (stream capture + graph + mempool) ✅
│  + no emu fallback (B-class L2 已移除)
└─────────────────────────────────────────────────────────┘
```

**验收标准:**
- `mock_gpu_driver` + `CudaStub` + `GpuDriverClient` 三条路径行为对称 (除 ioctl 真实路径)
- umd E2E `test_cu_graph` / `test_cu_mem_pool` / `test_cu_stream_capture` 全绿
- CI 包含 ASan/UBSan/TSan + UMD 默认模式

## 2. 当前架构

| 层 | stream_capture×3 (0x50-0x52) | graph×7 (0x53-0x59) | mem_pool×5+1 (0x60-0x68) |
|----|-----------------------------|---------------------|--------------------------|
| IGpuDriver 声明 | ✅ 默认 -1 | ✅ 默认 -1 | ✅ 默认 -1 |
| GpuDriverClient forwarding | ✅ 已实现 (PR #7, 行 569-810) | ✅ 已实现 | ✅ 已实现 |
| CudaStub stub 仿真 | ❌ 继承默认 -1 | ❌ 继承默认 -1 | ❌ 继承默认 -1 |
| UsrLinuxEmu sim | ✅ 已实现 (#20) | ✅ 已实现 | ✅ 已实现 (含 0x68) |
| UsrLinuxEmu emu 回退 | 🔴 **已移除** (B-class L2) | 🔴 已移除 | 🔴 已移除 |

**关键事实:**
- IGpuDriver 31→47 扩展 (21f71c9 + tadr-305)
- GpuDriverClient 48 overrides 落地 (PR #7, 07-06)
- UsrLinuxEmu sim primitives 落地 (#20, 07-06, 同日)
- 2026-08-04 emu 回退移除 (27 HAL fn-ptr 化) — 真实路径成为唯一路径
- CudaStub stub 模式全部返回 -1 → 测试路径单边断裂
- umd E2E 需重跑验证 (HAL 重构后行为变化)

## 3. 差距清单

| # | 差距项 | 严重程度 | 优先级 | 关联 |
|---|--------|---------|--------|------|
| G1 | CudaStub 16 方法未实现 (stub/real 行为不对称) | 🔴 高 | P0 | tadr-110 |
| G2 | umd E2E 未重跑 (HAL 重构 + emu 移除后回归) | 🔴 高 | P0 | tadr-111 |
| G3 | `query_queue` IGpuDriver 方法缺失 (UsrLinuxEmu 08-03 强化) | 🟠 中 | P1 | tadr-114 |
| G4 | `submit_graph` / `submit_memcpy` 返回 fence 语义与 Phase 1.5 fence_id 兼容性 | 🟡 中 | P1 | tadr-110 |
| G5 | `destroy_va_space` invalidation 语义强化 (08-03) 与现有实现一致性验证 | 🟡 中 | P1 | tadr-114 |
| G6 | `stream_id` u32↔u64 漂移记录 (ACCEPTED,触发再对齐) | 🟢 低 | P2 | tadr-306 (shared) |

## 4. 补齐路径

**Step 1 (P0):** 补齐 CudaStub 16 方法 stub 仿真 (与 GpuDriverClient 48 overrides 平行)
- 范围: stream_capture×3, graph×7, mem_pool×6
- 验证: `mock_gpu_driver` 测试 + `cuda_stub` 测试对称

**Step 2 (P0):** umd E2E 重跑 (test_cu_graph*, test_cu_mem_pool, test_cu_stream_capture)
- 验证 HAL 重构后行为变化
- ASan/UBSan/TSan 全 sanitizer 覆盖

**Step 3 (P1):** 新增 `query_queue` IGpuDriver 方法
- 新建独立 consumer-lens TADR (类似 Tadr-110 模式)
- ioctl 编号确认 + GpuDriverClient forwarding + 测试

**Step 4 (P1):** `submit_graph`/fence 语义与 Phase 1.5 对齐验证
- `destroy_va_space` invalidation 语义与 08-03 强化对照
- 现有 mock 测试不变,通过测试验证一致性

**Step 5 (P2):** stream_id u64 监控 (TADR-306 ACCEPTED, shared scope,触发条件驱动)

**依赖顺序:** Step 1 → Step 2 (并行) → Step 3 → Step 4 → Step 5 持续

**Phase 关联:** 独立于 roadmap Phase 3 (Multi-GPU/P2P, ⏸️ 待 H-7),Phase 3.1/3.2 是独立能力线 (TADR-110 D4 决策)。

## 5. 参考资料

- **本仓 TADR**: tadr-110, tadr-111, tadr-113, tadr-114 (test-fixture); tadr-206, tadr-207 (umd-evolution); tadr-301, tadr-305, tadr-306 (shared)
- **本仓架构文档**: docs/test-fixture/architecture/{capabilities,current,data-flow,layers}.md
- **本仓 roadmap**: docs/test-fixture/roadmap/README.md (Phase 3 ⏸️ Deferred); phase-1.5.md (fence_id)
- **UsrLinuxEmu**:
  - `21f71c9` (IGpuDriver 31→46 扩展); `138f15a` (#20 sim primitives); `f315c3e` (#27 MEM_POOL_EXPORT 0x68)
  - `489655a` (27 HAL fn-ptr); `63d2162` (5 removal proposals); `1ebf8f5` (ABI CI gate)
  - `85cfdcb` (REGISTER_GPU/MAP_QUEUE_RING E2E); `709188c` (DESTROY_VA_SPACE/QUERY_QUEUE semantics)
  - `02ae421` (stream_id u64 widening)
- **架构 SSOT**: UsrLinuxEmu `docs/02_architecture/post-refactor-architecture.md`