---
SCOPE: SHARED
STATUS: ACCEPTED
---

# TaskRunner TADR Index

TaskRunner 仓内 TADR 索引。本文件是 **canonical** source，
UsrLinuxEmu 端 `docs/00_adr/README.md` "TaskRunner TADR mirror" 段是 mirror。

## 范畴（Scopes）

TaskRunner TADR 分为 3 个 scope：

- **test-fixture**（默认主线）: `docs/test-fixture/adr/`
- **umd-evolution**（实验性愿景）: `docs/umd-evolution/adr/`
- **shared**（跨切面契约）: `docs/shared/adr/`

## test-fixture scope (1xx)

| TADR | 主题 | 状态 | 关联 ADR |
|------|------|------|----------|
| tadr-101 | Stub Tracker | ACCEPTED | — |
| tadr-102 | H-2.5 IGpuDriver consumer-lens | ACCEPTED | ADR-032 |
| tadr-103 | H-3 Phase 2 consumer-lens | ACCEPTED | ADR-033 |
| tadr-104 | R2 mapping contract | ACCEPTED | ADR-033 §R2 |
| tadr-105 | H-7 deferred mirror | ACCEPTED | ADR-034 |
| tadr-106 | test-fixture scope 明确化 | ACCEPTED | ADR-036 |
| tadr-109 | IGpuDriver 31 方法扩展 | ACCEPTED | ADR-033 |
| tadr-110 | IGpuDriver Phase 3.1/3.2 Real-Path consumer-lens (16 方法 ↔ ioctl 0x50-0x68) | PROPOSED | tadr-301, tadr-305 |
| tadr-111 | HAL L2 Foundation Removal consumer-lens (5 项 emu 移除 + 27 HAL fn-ptr 影响) | PROPOSED | tadr-110, tadr-302 |
| tadr-113 | Stage 4.4-4.6 ioctl Extension Tracking (SEM/IB_JUMP/Predicate/AQL/PDL/ContextType/Timeline 7 类待消费) | PROPOSED | tadr-301, tadr-302, ADR-047/049/051/052/046 |
| tadr-114 | REGISTER_GPU/CREATE_QUEUE/QUERY_QUEUE Semantic Alignment (08-03 派发表强化,query_queue 缺失) | PROPOSED | tadr-301, tadr-110, tadr-111, tadr-113 |

## umd-evolution scope (2xx)

| TADR | 主题 | 状态 | 关联 ADR |
|------|------|------|----------|
| tadr-201 | 统一调度器（原 tadr-001）| PROPOSED | — |
| tadr-202 | 分层设计（原 tadr-002）| PROPOSED | — |
| tadr-203 | 同步统一（原 tadr-003）| PROPOSED | — |
| tadr-204 | umd-evolution scope 明确化 | PROPOSED | ADR-036 |
| tadr-205 | UMD PoC 路线图（deferred）| PROPOSED | — |
| tadr-206 | IGpuDriver Phase 3.1/3.2 UMD-Evolution consumer-lens (shim 桥接缺口 5/16 = 31%) | PROPOSED | tadr-110, TADR-401 |
| tadr-207 | Register/Queue/QueryQueue UMD-Evolution consumer-lens (08-03 强化 shim 桥接审计 + query_queue 阻塞) | PROPOSED | tadr-114, TADR-401 |

## shared scope (107 + 108 + 3xx)

| TADR | 主题 | 状态 | 关联 ADR |
|------|------|------|----------|
| tadr-107 | shared 边界规则 | ACCEPTED | ADR-036 |
| tadr-108 | build mode selection | SUPERSEDED (by build-default-on) | ADR-035, ADR-036 |
| tadr-301 | IGpuDriver 28→46 方法契约 (H-5 新增, H-3.5 + Phase 3 扩展) | ACCEPTED | ADR-032, tadr-109 |
| tadr-302 | Sync Primitives 抽象 | ACCEPTED | — |
| tadr-303 | Error Handling 基础（Result\<T\>）| ACCEPTED | — |
| tadr-304 | Error Handling 策略层 | ACCEPTED | tadr-303 |
| tadr-305 | IGpuDriver::memPoolExportShareable 契约 (Phase 4 新增 47 方法) | ACCEPTED | tadr-301 |
| tadr-306 | stream_id u32 ↔ u64 Cross-Repo Drift (记录性,触发条件驱动,IGpuDriver 契约 exception) | PROPOSED | tadr-301 (exception) |
| tadr-307 | **IGpuDriver Kernel Module Extension**（PTX-EMU Image Executor HAL Backend 集成；**3 方法方案已 STALE**, per Oracle session `ses_ff2106f84ffeM2oItBEa9iu4hL` 识别违反 ADR-036（HAL 桥承担硬件行为提供者职责）；2026-08-20 精简为历史决策摘要 67 行 + redirect 到 [tadr-308](tadr-308-igpu-driver-vram-load.md) per ADR-035 §R2.4；不得作为实施依据）| STALE (superseded by tadr-308, 2026-08-18; slimmed 2026-08-20) | tadr-301, UsrLinuxEmu ADR-076 (since 🚫 Superseded by ADR-090 v2), PTX-EMU ADR-0029 §D8 (since pending amendment per ADR-090 v2 §C4) |
| tadr-308 | **IGpuDriver VRAM-Load Extension**（H2D DMA 路径；append-only 新增 1 个 IGpuDriver 方法 `load_kernel_module(image, image_size, *out_vram_addr)` 默认 `-ENOSYS`；CUmodule 重定义为 `uint64_t GPU VA`（per §Decision 1.2）；image_size 从 PTXIR 24B header (`string_table_offset + string_table_size`) 推断（per Oracle A′ 修订 2026-08-20）；kernel_name **由应用在 `cuModuleGetFunction` 传入**（不调 PTX-EMU ABI，零 UMD PTXIR 解析强制）；`cuModuleLoad` 同步改造走 VRAM-load 路径 + 独立 `next_func_id` 计数器（D6 owner 决策）；`cuModuleUnload` 走 `get_bo_gpu_va` 反查 + `free_bo` (G2 VA 翻译)；DISPATCH_KERNEL packet (T-013 G1) 携带 `vram_addr + kernel_name`；consumer-side 对偶 UsrLinuxEmu [ADR-090 v2](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/00_adr/adr-090-ptxir-via-h2d-dma-v2.md) (canonical ✅ Accepted)；[tadr-307 STALE](tadr-307-igpu-driver-kernel-module-extension.md) 替代)| PROPOSED | tadr-301 (exception), tadr-307 (STALE), [UsrLinuxEmu ADR-090 v2](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/00_adr/adr-090-ptxir-via-h2d-dma-v2.md) (canonical), [PTX-EMU ADR-0023](https://github.com/chisuhua/PTX-EMU/blob/main/docs/adr/ADR-0023-ptxir-binary-format.md) (24B header), [PTX-EMU ADR-0028](https://github.com/chisuhua/PTX-EMU/blob/main/docs/adr/ADR-0028-multi-kernel-manifest.md) (kernels[]), [PTX-EMU ADR-0029 §D1](https://github.com/chisuhua/PTX-EMU/blob/main/docs/adr/ADR-0029-ptxemu-image-executor.md#d1-新-abi-header--cpptlm_moduleh) (cpptlm_module.h 8 ABI, **§D8 待 amendment per ADR-090 v2 §C4**), CppTLM #19 |

## promotion scope (4xx)

> TADR-4xx reserved for **promotion proposals** (e.g., promote umd-evolution → ACCEPTED).
> 与 test-fixture/umd-evolution/shared 3-scope 平行，归档在 `docs/umd-evolution/adr/` 下。

| TADR | 主题 | 状态 | 关联 |
|------|------|------|------|
| tadr-401 | UMD-EVOLUTION → ACCEPTED promotion criteria (5-entry checklist) | PROPOSED | tadr-110, tadr-206, tadr-207 |

## 维护政策

本表是 canonical，UsrLinuxEmu 端 `docs/00_adr/README.md` 是 mirror。
新增 TADR 时：

1. 选择 scope（test-fixture / umd-evolution / shared）
2. 分配编号（1xx / 2xx / 3xx）
3. 写入本表
4. 按 ADR-035 §Rule 5.1 4 步同步到 UsrLinuxEmu

## 跨仓引用

- UsrLinuxEmu 端 mirror: `../../../../docs/00_adr/README.md`
- 跨仓同步协议: `../../../../docs/00_adr/adr-035-governance-policy.md`
- AGENTS.md §Scope Classification: `../../../AGENTS.md`

最后更新: 2026-08-20 (TADR-308 大幅修订 per Oracle session `ses_fe0443831ffenUxpEQxZqWE8Cp` A' 修订: §A 24B header 真实格式 + §1.5 kernel_name 由应用传入 + §Decision 1.2 CUmodule=VA 重定义 + 5 atomic commits 落地; TADR-307 精简为 67 行历史摘要 per ADR-035 §R2.4)