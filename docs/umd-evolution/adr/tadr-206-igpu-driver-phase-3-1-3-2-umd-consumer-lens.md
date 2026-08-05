---
SCOPE: UMD-EVOLUTION
STATUS: PROPOSED
---

# TADR-206: IGpuDriver Phase 3.1/3.2 UMD-Evolution Consumer-Lens

**状态**: 🔄 Proposed
**日期**: 2026-08-04 (创建)
**提案人**: Chi Suhua
**评审者**: (待定)
**关联 ADR (UsrLinuxEmu)**: (无独立 canonical — sim 原语经 PR #20)
**关联 Source**: src/umd/libcuda_shim/{cu_graph.cpp, cu_mem_pool.cpp, cu_stream_capture.cpp};
include/shared/igpu_driver.hpp:362-529

---

## Context

UMD-EVOLUTION 视角的 Phase 3.1/3.2 consumer-lens 镜像 (test-fixture 对照版见 tadr-110)。
与 tadr-110 关注接口 ↔ ioctl 映射不同,本 TADR 关注 **umd shim 实际桥接到 IGpuDriver 的范围**:
哪些 `cu_*` API 已通过 IGpuDriver 走真实路径,哪些仍由 shim 内部管理。

umd-evolution 现状 ([roadmap/current-status.md](../roadmap/current-status.md)):
- Phase 3.1+3.2 已 COMPLETE (PR #7, 4-step coord done)
- Phase 4 real-impl-bridge 已 COMPLETE (PR #8)
- UMD build default-on (87c9879, 2026-07-10)

但 shim 内部状态管理存在 **桥接缺口** (见 Decision §2 状态表)。

## Decision

1. **umd-evolution mirror**: 与 tadr-110 互补,tadr-110 记录接口映射,本 TADR 记录 shim 桥接范围。
2. **shim 桥接状态表**:

| IGpuDriver 方法 | UMD shim 桥接 | shim 实现 |
|-----------------|----------------|-----------|
| `mem_pool_alloc` | ✅ 桥接 (cu_mem_pool.cpp:75) | REAL_IMPL via g_gpu_client |
| `mem_pool_alloc_async` | ✅ 桥接 (cu_mem_pool.cpp:95) | REAL_IMPL via g_gpu_client |
| `mem_pool_free_async` | ✅ 桥接 (cu_mem_pool.cpp:107) | REAL_IMPL via g_gpu_client |
| `mem_pool_export_shareable` | ✅ 桥接 (cu_mem_pool.cpp:151) | REAL_IMPL via g_gpu_client (Phase 4) |
| `submit_graph` | ✅ 桥接 (cu_graph.cpp:141) | REAL_IMPL via g_gpu_client |
| `graph_create` | ❌ **内部管理** (cu_graph.cpp:42 `next_graph_id`) | STUB (local graph_nodes map) |
| `graph_destroy` | ❌ 内部管理 (cu_graph.cpp:78) | STUB |
| `graph_add_kernel_node` | ❌ 内部管理 (cu_graph.cpp:112) | STUB |
| `graph_add_memcpy_node` | ❌ 内部管理 (cu_graph.cpp:131) | STUB |
| `graph_instantiate` | ❌ 内部管理 | STUB |
| `destroy_graph_exec` | ❌ 内部管理 | STUB |
| `stream_capture_begin` | ❌ **内部管理** (cu_stream_capture.cpp:29 `next_graph_id`) | STUB (local state machine) |
| `stream_capture_end` | ❌ 内部管理 | STUB |
| `stream_capture_status` | ❌ 内部管理 | STUB |
| `mem_pool_create` | ⚠️ 未直接桥接 (通过 `cuMemPoolCreate` 走不同路径?) | 需进一步确认 |
| `mem_pool_destroy` | ⚠️ 未直接桥接 | 需进一步确认 |

3. **桥接缺口影响**:
   - graph 系列: shim 内部 graph_nodes map → 与 UsrLinuxEmu sim 端 graph 状态不同步,
     multi-process / cross-stream 场景下行为可能不一致
   - stream_capture 系列: shim 状态机本地化 → 与 UsrLinuxEmu sim 的 capture 语义解耦,
     需要确保两边状态转换语义对齐
4. **STATUS=PROPOSED**,实施路径:
   - **Step 1**: 桥接 `graph_create`/`graph_destroy`/`graph_add_*_node`/`graph_instantiate`/
     `destroy_graph_exec` 到 IGpuDriver (cu_graph.cpp)
   - **Step 2**: 桥接 `stream_capture_begin`/`end`/`status` 到 IGpuDriver (cu_stream_capture.cpp)
   - **Step 3**: 验证 mem_pool_create/destroy 路径 (cu_mem_pool.cpp)
   - **Step 4**: umd E2E 全绿 + sanitizer 全覆盖 → 升 ACCEPTED

## Consumer-Lens

UMD-EVOLUTION 侧落地: 与 tadr-110 共享 16 方法 ↔ ioctl 0x50-0x68 映射表(以 IGpuDriver 为入口),
shim 层需逐方法选择"桥接到 IGpuDriver"或"内部管理"。当前桥接覆盖率约 31% (5/16),与
test-fixture 侧 100% (GpuDriverClient 48 overrides) 形成对照。

## Consequences

### 正面

- 明确 shim 桥接缺口,避免 UMD-EVOLUTION promote 时遗漏
- 桥接到 IGpuDriver 后, shim 行为与 UsrLinuxEmu sim 强一致,跨仓联调成本降低
- 与 tadr-110 形成接口/实现两侧完整覆盖

### 负面 / 风险

- graph/stream_capture 桥接需重写 shim 内部状态管理逻辑,工作量大
- 桥接后 shim 依赖 IGpuDriver 可用性, CudaStub 缺口 (tadr-110) 会反向影响 umd 测试
- 与 UMD-EVOLUTION → ACCEPTED promote (TADR-401) 时序协调

### 实施路径备注

- 本 TADR 与 tadr-110 实施窗口可重叠 (同 Phase 3.1/3.2 时间线)
- cu_graph.cpp / cu_stream_capture.cpp 重写时需保持 API 兼容 (cuGraph*/cuStream* 入口不变)
- 桥接后内部状态数据迁移需额外处理 (existing graph handles)

## 跨引用

- **tadr-110**: IGpuDriver Phase 3.1/3.2 Real-Path consumer-lens (test-fixture,接口映射)
- **tadr-301**: IGpuDriver 47 方法契约 (shared)
- **tadr-305**: `mem_pool_export_shareable` (shared)
- **tadr-306**: stream_id u32↔u64 Cross-Repo Drift (shared)
- **TADR-401**: UMD-EVOLUTION → ACCEPTED promotion criteria (umd-evolution)
- **关联 Source**: `src/umd/libcuda_shim/cu_graph.cpp`, `cu_graph_exec.cpp`, `cu_graph_node.cpp`,
  `cu_mem_pool.cpp`, `cu_stream_capture.cpp`; `include/shared/igpu_driver.hpp`

---

**最后更新**: 2026-08-04