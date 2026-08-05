---
SCOPE: TEST-FIXTURE
STATUS: PROPOSED
---

# TADR-110: IGpuDriver Phase 3.1/3.2 Real-Path Consumer-Lens

**状态**: 🔄 Proposed
**日期**: 2026-08-04 (创建)
**提案人**: Chi Suhua
**评审者**: (待定)
**关联 ADR (UsrLinuxEmu)**: 无独立 canonical ADR — sim 原语经 PR #20,ioctl 契约见 gpu_ioctl.h
**关联 Source**: include/shared/igpu_driver.hpp:362-529; include/test_fixture/gpu_driver_client.h:569-810

---

## Context

IGpuDriver 于 2026-07-06 从 31 方法扩展至 46 (commit `21f71c9`, tadr-301),新增 Phase 3.1/3.2 的
stream capture / graph / mem_pool 共 15 方法,后加 `mem_pool_export_shareable` (tadr-305) 至 47。
UsrLinuxEmu sim 同日 (PR #20, `138f15a`) 提供对应原语,ioctl 编号 0x50-0x59 (stream/graph 10 个)
+ 0x60-0x64/0x68 (mem_pool 6 个)。TaskRunner 侧 `GpuDriverClient` 已由 PR #7 实现全部 15 个
ioctl forwarding override,但 `CudaStub` 未实现 (继承接口默认 -1),stub/real 行为不对称。
2026-08-04 UsrLinuxEmu B-class L2 (27 HAL fn-ptr 化) 移除 `stream_capture_emu`/`mem_pool_emu`
回退,真实路径成为唯一路径。

## Decision

1. 本 TADR 以 consumer-lens 记录 16 方法 ↔ ioctl 映射契约 (tadr-104 模式),不重复上游决策文本。
2. 映射表:

| IGpuDriver 方法 | ioctl | 编号 |
|-----------------|-------|------|
| `stream_capture_begin` | STREAM_CAPTURE_BEGIN | 0x50 |
| `stream_capture_end` | STREAM_CAPTURE_END | 0x51 |
| `stream_capture_status` | STREAM_CAPTURE_STATUS | 0x52 |
| `graph_create` | GRAPH_CREATE | 0x53 |
| `graph_destroy` | GRAPH_DESTROY | 0x54 |
| `graph_add_kernel_node` | GRAPH_ADD_KERNEL_NODE | 0x55 |
| `graph_add_memcpy_node` | GRAPH_ADD_MEMCPY_NODE | 0x56 |
| `graph_instantiate` | GRAPH_INSTANTIATE | 0x57 |
| `submit_graph` | GRAPH_LAUNCH | 0x58 |
| `destroy_graph_exec` | GRAPH_DESTROY_EXEC | 0x59 |
| `mem_pool_create` | MEM_POOL_CREATE | 0x60 |
| `mem_pool_destroy` | MEM_POOL_DESTROY | 0x61 |
| `mem_pool_alloc` | MEM_POOL_ALLOC | 0x62 |
| `mem_pool_alloc_async` | MEM_POOL_ALLOC_ASYNC | 0x63 |
| `mem_pool_free_async` | MEM_POOL_FREE_ASYNC | 0x64 |
| `mem_pool_export_shareable` | MEM_POOL_EXPORT | 0x68 (tadr-305) |

3. 落地状态: `GpuDriverClient` ✅ (PR #7); `CudaStub` ❌ 缺口; UsrLinuxEmu emu 回退已移除 →
   真实路径唯一,测试须走真实路径。
4. 实施路径: 补齐 `CudaStub` 16 方法 stub 仿真 → E2E 测试覆盖 → 通过后升 ACCEPTED。
5. Phase 3.1/3.2 是独立能力线,不受 roadmap Phase 3 (Multi-GPU/P2P, ⏸️ 待 H-7) 门控。

## Consumer-Lens

TaskRunner 侧落地: `GpuDriverClient` 内联 ioctl forwarding (PR #7, 行 569-810); `CudaStub` stub
语义待实现; umd-evolution shim (`cu_stream_capture.cpp`/`cu_graph*.cpp`/`cu_mem_pool.cpp`) 在底层
返回 -1 时降级,真实路径就绪后自动生效。ioctl 结构体字段与参数语义以 UsrLinuxEmu `gpu_ioctl.h` 为 canonical。

## Consequences

### 正面

- 16 方法 ↔ ioctl 映射单一事实来源,消除跨仓契约模糊
- 记录 emu 移除影响,测试路径真实化决策明确
- 与 tadr-301 (47 方法契约)、tadr-305 (export) 形成完整文档链

### 负面 / 风险

- `CudaStub` 缺口: stub 模式 Phase 3.1/3.2 功能不可用 (返回 -1)
- `stream_id` u64 漂移 (UsrLinuxEmu 06-26 已扩) 未对齐 — 另见 TADR-306
- Stage 4.4-4.6 新 ioctl (SEM/IB_JUMP/SET_PREDICATE/PDL/context_type) 无对应方法 — 另见 TADR-113

### 实施路径备注

- PR #7 已落地 `GpuDriverClient` forwarding; `submit_graph` (GRAPH_LAUNCH) 返回 int64_t fence,与 `submit_batch` 语义一致

## 跨引用

- **tadr-301**: IGpuDriver 47 方法契约 (shared)
- **tadr-305**: `mem_pool_export_shareable` (0x68)
- **tadr-104**: R2 mapping contract (consumer-lens 先例)
- **UsrLinuxEmu**: PR #20 (`138f15a`), PR #27 (`f315c3e`), `gpu_ioctl.h` (0x50-0x68)
- **B-class L2**: `489655a` (27 fn-ptrs), `63d2162` (5 removal proposals)
- **关联 Source**: `include/shared/igpu_driver.hpp`:362-529; `include/test_fixture/gpu_driver_client.h`:569-810

---

**最后更新**: 2026-08-04
