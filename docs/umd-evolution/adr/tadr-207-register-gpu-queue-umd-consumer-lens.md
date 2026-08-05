---
SCOPE: UMD-EVOLUTION
STATUS: PROPOSED
---

# TADR-207: IGpuDriver Register/Queue/QueryQueue UMD-Evolution Consumer-Lens

**状态**: 🔄 Proposed
**日期**: 2026-08-04 (创建)
**提案人**: Chi Suhua
**评审者**: (待定)
**关联 ADR (UsrLinuxEmu)**: (无独立 canonical — 见 08-03 E2E 强化 commits)
**关联 Source**: src/umd/libcuda_shim/{cu_init.cpp, cu_device.cpp, cu_ctx.cpp, cu_stream.cpp};
include/shared/igpu_driver.hpp:register_gpu/create_queue/destroy_queue/destroy_va_space

---

## Context

UMD-EVOLUTION 视角的 register_gpu / create_queue / query_queue 消费者镜像
(test-fixture 对照版见 tadr-114)。UsrLinuxEmu 2026-08-03 强化派发表
(`85cfdcb`: REGISTER_GPU + MAP_QUEUE_RING E2E; `709188c`: DESTROY_VA_SPACE invalidation +
QUERY_QUEUE E2E semantics),并通过 ABI CI gate (`1ebf8f5`) 保证 ioctl 派发表一致
(37→38 handlers)。

UMD shim 中 `cu*` API 与 IGpuDriver register/queue 系列方法的桥接关系
需从 umd-evolution 视角单独审视 (test-fixture 视角关注 IGpuDriver 表面层)。

## Decision

1. **umd-evolution mirror**: 与 tadr-114 互补,本 TADR 记录 shim 端 register/queue 系列
   方法的实际使用与桥接状态。
2. **shim 桥接状态表**:

| IGpuDriver 方法 | UMD shim 入口 | shim 桥接 | 备注 |
|-----------------|---------------|-----------|------|
| `register_gpu` | (推测 `cuDevice*` API 链路) | ⚠️ 需确认 | va_space_handle + gpu_id 注册路径 |
| `create_queue` | (推测 `cuStreamCreate` / `cuCtxCreate`) | ⚠️ 需确认 | queue_type 决定 stream 类型 |
| `destroy_queue` | (推测 `cuStreamDestroy`) | ⚠️ 需确认 | — |
| `destroy_va_space` | (推测 `cuDevicePrimaryCtxRelease`) | ⚠️ 需确认 | 08-03 invalidation 强化需验证一致 |
| `query_queue` | ❌ **IGpuDriver 无方法** | 阻塞 | UsrLinuxEmu 08-03 强化,TaskRunner 缺失 |

3. **query_queue 缺口的 umd 视角**:
   - 当 UMD shim 需要查询队列状态/属性时,无 IGpuDriver 方法可调用
   - 候选 shim 入口: `cuStreamQuery` (返回 CUresult not ready/success),需扩展为属性查询
   - 实施时: 新建独立 consumer-lens TADR (类似 tadr-110 模式),定义签名/派发/参数语义
4. **STATUS=PROPOSED**,实施路径:
   - **Step 1**: 审计 cu_init.cpp / cu_device.cpp / cu_ctx.cpp / cu_stream.cpp 中 register/queue
     系列方法的实际桥接路径 (填补 ⚠️ 标记)
   - **Step 2**: 验证现有桥接与 UsrLinuxEmu 08-03 强化语义一致
   - **Step 3**: 新增 `query_queue` IGpuDriver 方法 (与 tadr-114 同步)
   - **Step 4**: shim 端添加 query_queue 入口 → umd E2E → 升 ACCEPTED

## Consumer-Lens

UMD-EVOLUTION 侧落地: 现有 cu* API 通过 IGpuDriver register/queue 路径访问 UsrLinuxEmu sim,
需在 shim 端确认每条路径的桥接完整性 (尤其 08-03 强化后)。`query_queue` 缺口是 umd 视角的
**最严重阻塞** — 阻塞任何需要查询队列属性的 UMD API 实施 (如 `cuStreamGetAttribute`)。

## Consequences

### 正面

- 明确 register/queue 系列方法的 shim 桥接审计点
- `query_queue` 需求登记与 tadr-114 同步, 避免 UMD shim 阻塞
- 与 tadr-401 (UMD promote) 时序协调

### 负面 / 风险

- shim 端 register/queue 桥接审计需逐文件检查,工作量大
- `query_queue` IGpuDriver 签名变更需 tadr-301 §Stability Rules 协调 (允许新增,backward-compatible)
- 08-03 强化语义变化可能导致现有 shim 行为差异

### 实施路径备注

- 实施时序与 tadr-114 一致 (同一 Phase 3.1/3.2 窗口)
- `query_queue` 新增 IGpuDriver 方法 = backward-compatible per tadr-301 Rule 2
- shim 端 `cuStreamGetAttribute` 等 API 是实施候选触发点

## 跨引用

- **tadr-114**: REGISTER_GPU/CREATE_QUEUE/QUERY_QUEUE Semantic Alignment (test-fixture,接口对齐)
- **tadr-110**: IGpuDriver Phase 3.1/3.2 Real-Path consumer-lens (test-fixture)
- **tadr-206**: IGpuDriver Phase 3.1/3.2 UMD-Evolution Consumer-Lens (umd-evolution, 姐妹篇)
- **tadr-301**: IGpuDriver 47 方法契约 (shared,允许新增方法)
- **TADR-401**: UMD-EVOLUTION → ACCEPTED promotion criteria (umd-evolution)
- **UsrLinuxEmu**: `85cfdcb` (REGISTER_GPU/MAP_QUEUE_RING E2E), `709188c` (DESTROY_VA_SPACE/
  QUERY_QUEUE semantics), `1ebf8f5` (ABI CI gate), `541917d` (handler count 37→38)
- **关联 Source**: `src/umd/libcuda_shim/{cu_init.cpp, cu_device.cpp, cu_ctx.cpp, cu_stream.cpp}`;
  `include/shared/igpu_driver.hpp`:register_gpu/create_queue/destroy_queue/destroy_va_space

---

**最后更新**: 2026-08-04