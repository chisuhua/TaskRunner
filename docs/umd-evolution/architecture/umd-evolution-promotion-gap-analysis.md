---
SCOPE: UMD-EVOLUTION
STATUS: PROPOSED
DATE: 2026-08-04
RELATED:
  - tadr-401 (UMD-EVOLUTION → ACCEPTED promotion criteria)
  - tadr-108 (build mode selection, SUPERSEDED)
  - tadr-110 (Phase 3.1/3.2 Real-Path consumer-lens, test-fixture)
  - tadr-206 (Phase 3.1/3.2 UMD-Evolution consumer-lens)
  - tadr-207 (Register/Queue/QueryQueue UMD-Evolution consumer-lens)
  - tadr-204 (umd-evolution scope 明确化)
---

# 架构差距分析: umd-evolution-promotion

> **生成日期**: 2026-08-04
> **状态**: 草案 (PROPOSED)
> **关联 ADR**: tadr-401 (promotion criteria); tadr-204 (scope 明确化); tadr-108 (build mode, SUPERSEDED)
> **跨仓**: UsrLinuxEmu ADR-035 (governance policy)

## 1. 目标架构

UMD-EVOLUTION scope 从 PROPOSED → ACCEPTED:

```
┌─────────────────────────────────────────────────────────┐
│  External Caller (CLI / Tests / LD_PRELOAD shim)        │
│  ↓ 默认 build mode (TASKRUNNER_BUILD_MODE=umd-evolution) ✅
├─────────────────────────────────────────────────────────┤
│  Phase 1+2: CudaRuntimeApi + libcuda_shim (cu_* 17 文件)
│  + Phase 3: cu_graph/cu_mem_pool/cu_stream_capture (真实路径)
│  + Phase 3.3: Event/Texture/CuArray (已 ✅ 07-08)
├─────────────────────────────────────────────────────────┤
│  IGpuDriver 47 方法 (完整覆盖)
│  + Stage 4 ioctl 消费 (TADR-113 追踪)
├─────────────────────────────────────────────────────────┤
│  GpuDriverClient + CudaStub (对称,见 G1)
├─────────────────────────────────────────────────────────┤
│  UsrLinuxEmu sim (Stage 4.4-4.6 完整 + B-class L2 真实路径)
└─────────────────────────────────────────────────────────┘
```

**验收标准 (源自 tadr-401):**
- UMD build 默认开启 (已 ✅ 87c9879, 2026-07-10)
- libcuda_shim 完整实现 (cu_* 17 文件 + Phase 3 扩展)
- CI 覆盖: ASan/UBSan/TSan + UMD 默认模式 + 跨 arch build
- 全 E2E 通过 (test_cu_graph*, test_cu_mem_pool, test_cu_stream_capture 等)
- ABI 与 tadr-301/305 契约一致
- docs audit 通过 (tools/docs-audit.sh)
- 跨仓 mirror 同步 (UsrLinuxEmu docs/00_adr/README.md)

## 2. 当前架构

**已完成 (2026-07-08 ~ 07-20):**
- ✅ Phase 3.3 Event timing + Texture/Surface (498265c, 07-08)
- ✅ sanitizer wiring (ASan/UBSan/TSan, 28f1790, 07-09)
- ✅ UMD build default-on BREAKING (87c9879, 07-10)
- ✅ g_gpu_client Meyers-singleton fallback (4689997~b210643, 07-11)
- ✅ TADR-401 promotion criteria 落地 (5206714, 07-12)
- ✅ L1↔L2 bridge E2E test (d94719c, 07-18)
- ✅ CUDA E2E real-path integration (42ad77e, 07-20)
- ✅ scheduler: fence async semantics + wait_fence timeout (f6becce, Phase C, 07-20)
- ✅ scheduler: gpu_va as device_ptr for Puller HAL addressing (d8c76bd, 07-20)

**未完成 (差距):**
- ❌ CudaStub 16 方法缺口 (与 test-fixture gap #1 共享)
- ❌ umd E2E 未重跑验证 (Stage 4 HAL 重构后)
- ❌ Stage 4 ioctl 7 类未消费 (G2-G6 in test-fixture gap #2)
- ❌ CI 默认 UMD 模式的 sanitizer 全覆盖验证
- ❌ API parity 检查 (vs CUDA Runtime / Driver API)

## 3. 差距清单

| # | 差距项 | 严重程度 | 优先级 | 关联 |
|---|--------|---------|--------|------|
| G1 | CudaStub 16 方法缺口 (跨 test-fixture gap #1) | 🔴 高 | P0 | tadr-110, tadr-206 |
| G2 | umd E2E 未重跑 (Stage 4 HAL 重构后,跨 test-fixture gap #1) | 🔴 高 | P0 | tadr-111 |
| G3 | Stage 4 ioctl 7 类未消费 (跨 test-fixture gap #2,G1-G7) | 🟠 中 | P1 | tadr-113 |
| G4 | CI 默认 UMD 模式 + sanitizer 全覆盖验证 | 🟠 中 | P1 | tadr-401 |
| G5 | Phase 3.1/3.2 UMD shim 真实路径联调 (cu_graph/cu_mem_pool/cu_stream_capture) | 🟠 中 | P1 | tadr-110, tadr-206 |
| G6 | API parity 检查 (vs CUDA Runtime / Driver API 完整度) | 🟡 中 | P2 | tadr-401 |
| G7 | docs audit + 跨仓 mirror 同步 (UsrLinuxEmu docs/00_adr/README.md) | 🟡 低 | P2 | tadr-401 |
| G8 | Phase 1.5+ 待 Stage 4 能力消费 (Semaphore/Timeline 桥接) | 🟢 低 | P2 | tadr-113 |

## 4. 补齐路径

**Step 1 (P0):** 补齐 CudaStub 16 方法 + umd E2E 重跑
- 与 test-fixture gap #1 G1/G2 共享实施
- umd 视角补充: shim 路径在底层返回 -1 时降级,真实路径就绪后自动生效

**Step 2 (P1):** Stage 4 ioctl 7 类消费 (与 test-fixture gap #2 共享)
- 优先 PDL + IB_JUMP (与 graph 同步)
- 优先 Semaphore + Timeline (与 fence 同步)
- 每类别消费时新建 TADR,UMD shim 同步添加入口

**Step 3 (P1):** CI 默认 UMD 模式 + sanitizer 全覆盖
- 修改 .github/workflows/ 默认 TASKRUNNER_BUILD_MODE=umd-evolution
- ASan/UBSan/TSan 三个 job 全绿
- 跨 arch (x86_64 / aarch64) build matrix

**Step 4 (P1):** Phase 3.1/3.2 UMD shim 真实路径联调
- cu_graph_launch / cuGraphLaunch → IGpuDriver submit_graph
- cuMemPoolAlloc → IGpuDriver mem_pool_alloc
- cuStreamBeginCapture → IGpuDriver stream_capture_begin
- 跨仓联调 (TaskRunner sim + UsrLinuxEmu B-class L2 验证)

**Step 5 (P2):** API parity 检查 + docs audit + 跨仓 mirror 同步
- 生成 docs/umd-evolution/api-parity.md (vs CUDA Driver API 完整度表)
- tools/docs-audit.sh 通过
- 更新 UsrLinuxEmu docs/00_adr/README.md mirror 表 (TADR-401 + tadr-204)

**Step 6 (P2):** TADR-401 promote criteria 逐项核对 → 提交 promote change → STATUS 升 ACCEPTED

**依赖顺序:** Step 1 → Step 2 (并行 Step 3/4) → Step 5 → Step 6

**作用域:** 本 gap-analysis 聚焦 UMD-EVOLUTION scope promote 决策,与 test-fixture gap #1/#2 共享 G1/G2/G3 实施。

## 5. 参考资料

- **本仓 TADR**: tadr-401 (promotion criteria); tadr-204 (scope 明确化); tadr-108 (build mode, SUPERSEDED); tadr-110/111/113/114 (test-fixture consumer-lens); tadr-206/207 (umd-evolution shim 桥接); tadr-301/305/306 (shared)
- **本仓文档**:
  - docs/umd-evolution/README.md (scope 规则)
  - docs/umd-evolution/roadmap/ (UMD PoC 路线图,deferred)
  - docs/umd-evolution/architecture/{README,runtime-layering}.md
  - docs/07-integration/ (UsrLinuxEmu cross-repo integration)
- **本仓 commits** (已完成项):
  - `87c9879` UMD default-on BREAKING
  - `498265c` Phase 3.3 Event+Texture merge
  - `28f1790` sanitizer wiring
  - `d94719c` L1↔L2 bridge E2E
  - `42ad77e` CUDA E2E real-path integration
  - `5206714` TADR-401 promotion criteria
- **跨仓**: UsrLinuxEmu `docs/00_adr/README.md` (TaskRunner TADR mirror 表); ADR-035 (governance policy)
- **工具**: `tools/docs-audit.sh` (pre-commit hook)
- **架构 SSOT**: UsrLinuxEmu `docs/02_architecture/post-refactor-architecture.md`