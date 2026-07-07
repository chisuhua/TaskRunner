# TaskRunner-UsrLinuxEmu 接口统一同步计划

**版本**: v2.4（Phase 4 real-impl-bridge, 270 tests passing）
**日期**: 2026-07-07
**维护者**: UsrLinuxEmu Architecture Team + TaskRunner owner
**前置**: H-2.5 ✅ + H-3 ✅ shippable（2026-06-23）+ Step 1+2 ✅ merged（2026-07-06）

---

## 一、协调工作流

### 1.1 核心原则

```
1. 接口定义 (Canonical Source)
   └── UsrLinuxEmu 定义 gpu_ioctl.h，是唯一真源

2. 双向依赖，同步点驱动
   └── UsrLinuxEmu 实现驱动，TaskRunner 实现客户端
   └── 在定义的同步点等待对方输入，其他时间并行开发

3. 契约优先 (Contract-First)
   └── ADR-015 定义 System C 接口契约
   └── 通过 headless 测试验证契约一致性
```

### 1.2 当前架构状态

```
┌────────────────────────────────────────────────────────────┐
│ H-3 (2026-06-23): 5 Phase 2 ioctl wrapper 实现完成           │
│   - create_va_space / destroy_va_space / register_gpu        │
│   - create_queue / destroy_queue                            │
│   - GpuDriverClient 真实实现 + CudaStub mock                 │
│   - 12 doctest cases (test_gpu_phase2.cpp)                   │
│   - 2 CLI subcommand (cuda_va_space / cuda_queue)            │
│   - 9 commits (241f3ed..8625b82), 双仓 sync 完成            │
└────────────────────────────────────────────────────────────┘
```

### 1.3 H-3.5~H-3.8 补丁修复（2026-06-26）

```
┌────────────────────────────────────────────────────────────┐
│ H-3.5 (2026-06-25): CudaStub guard verification follow-up   │
│   - 2 dynamic_cast removed (MockGpuDriver + IGpuDriver)     │
│   - IGpuDriver extended to 31 methods (tadr-109)           │
│   - TADR-105 agenda tracking                               │
└────────────────────────────────────────────────────────────┘
┌────────────────────────────────────────────────────────────┐
│ H-3.6 (2026-06-25): ADR-034 Issue #3 → attached_queues    │
│   - UsrLinuxEmu: bf8192f (pushbuffer VA+Queue validation) │
│   - UsrLinuxEmu: 09ae1b0 (4 test cases)                   │
│   - TaskRunner: f3f52d8 (coordination doc)                │
└────────────────────────────────────────────────────────────┘
┌────────────────────────────────────────────────────────────┐
│ H-3.7 (2026-06-25): ADR-034 Issue #2 → ioctl path bypass  │
│   - UsrLinuxEmu: 392a496 (route pushbuffer via GpuQueueEmu)│
│   - TaskRunner: 73390ae (coordination + H-3.7 kickoff)    │
└────────────────────────────────────────────────────────────┘
┌────────────────────────────────────────────────────────────┐
│ H-3.8 (2026-06-26): ADR-034 Issue #1 → stream_id u32→u64 │
│   - UsrLinuxEmu: 02ae421 (widen stream_id + compat alias) │
│   - TaskRunner: 9e3db2e (ABI coordination + test design)  │
│   - TaskRunner: 5ee250a (tadr-105 §Issue #1 → Accepted)  │
└────────────────────────────────────────────────────────────┘
```

### 1.4 Phase 3 Step 3 完成（2026-07-07）

```
┌────────────────────────────────────────────────────────────┐
│ Step 3 (2026-07-07): cuStreamCapture + cuGraph + cuMemPool shim │
│   - C2: 15 GpuDriverClient forwarding overrides (header inline)│
│   - C3: 3 cuStream* capture APIs REAL_IMPL (cu_stream_capture.cpp)│
│   - C4: 11 cuGraph* APIs REAL_IMPL (cu_graph.cpp + cu_graph_node.cpp│
│        + cu_graph_exec.cpp, removed cuGraphCreate stub from cu_mem.cpp)│
│   - C5: 8 cuMemPool* APIs REAL_IMPL (cu_mem_pool.cpp)         │
│   - C6: 83 new E2E test cases (3 binaries, 225 total tests)│
│   - C7: this sync-plan.md update + archive handoff           │
│                                                              │
│   7 atomic commits on branch phase3-step3-shim-and-forwarding│
│   Build: 0 warnings, 225/225 tests pass (142 baseline + 83 new)│
│   11 cuGraph* + 8 cuMemPool* + 4 cuStream* capture symbols exported│
│                                                              │
│   Next: notify UsrLinuxEmu owner → Step 4 (submodule bump)  │
└────────────────────────────────────────────────────────────┘
```

### 1.5 Phase 4 真实化（2026-07-07）

```
┌────────────────────────────────────────────────────────────┐
│ Phase 4 (2026-07-07): cuGraphLaunch + cuMemPool* REAL bridge│
│                                                              │
│   跨仓工作：                                                  │
│     - UsrLinuxEmu: 新增 GPU_IOCTL_MEM_POOL_EXPORT (0x68)    │
│       IOCTL + ADR-039                                        │
│     - TaskRunner: IGpuDriver 46→47 方法 + tadr-302 新增      │
│                                                              │
│   - C8: cuGraphLaunch REAL bridge                            │
│     (Phase 3 PoC → real submit_graph IOCTL 0x58)             │
│   - C9: 5 cuMemPool* APIs REAL bridge                       │
│     (Alloc/AllocAsync/FreeAsync/ExportToShareableHandle)    │
│   - C10: g_gpu_client 类型 IGpuDriver*                       │
│     + nullptr fallback (CUDA_ERROR_NOT_INITIALIZED)          │
│   - C11: 13 new E2E test cases (mock-injection path)         │
│   - C12: tadr-301/302 + sync-plan update                     │
│                                                              │
│   Build: 0 warnings, 270/270 tests pass                      │
│   Next: notify UsrLinuxEmu owner → submodule bump            │
└────────────────────────────────────────────────────────────┘
```

## 二、GitHub Issue 追踪

### 2.1 TaskRunner 仓库 Issues

| Issue | 主题 | 状态 |
|-------|------|------|
| #5 | Phase 1 实现清单 | ✅ UsrLinuxEmu 已完成 + TaskRunner H-3 完成 |

### 2.2 UsrLinuxEmu 仓库 Issues

| Issue | 主题 | 状态 |
|-------|------|------|
| #11 | VFS 单例问题 | ✅ 已修复 |
| #12 | Phase 1.5 fence_id 扩展（S3.5）| ✅ 已完成（2026-05-13）|
| #13 | Teardown SIGSEGV | ✅ 已修复（2026-05-09, commit dd81e5c）|

---

## 三、Phase 1.5、Phase 2 及 H-3 补丁已全部完成

### 3.1 Phase 1.5 (S3.5 fence_id 扩展)

| 任务 | 状态 | 完成日期 |
|------|------|----------|
| `gpu_pushbuffer_args.fence_id` 字段 | ✅ 已定义 | 2026-05-08 |
| `gpu_device_info` 增加 warp_size 等字段 | ✅ 已完成（struct 144 字节，11 新字段）| 2026-05-13 |
| Issue #13: Teardown SIGSEGV 修复 | ✅ 已修复（commit dd81e5c）| 2026-05-09 |

### 3.2 Phase 2 (S5 + H-3)

| 任务 | 状态 | 提交链 |
|------|------|--------|
| VA Space/Queue 抽象设计（S5）| ✅ 完成 | UsrLinuxEmu commit c64301c（2026-06-19）|
| `GPU_IOCTL_CREATE_VA_SPACE` | ✅ 完成 | TaskRunner 241f3ed..8625b82（2026-06-23）|
| `GPU_IOCTL_DESTROY_VA_SPACE` | ✅ 完成 | 同上 |
| `GPU_IOCTL_REGISTER_GPU` | ✅ 完成 | 同上 |
| `GPU_IOCTL_CREATE_QUEUE` | ✅ 完成 | 同上 |
| `GPU_IOCTL_DESTROY_QUEUE` | ✅ 完成 | 同上 |
| CLI `cuda_va_space` / `cuda_queue` subcommands | ✅ 完成 | 同上 |

### 3.3 跨仓 sync 历史

| Change | 状态 | commit |
|--------|------|--------|
| H-1 (h1-pushbuffer-validation-closeout) | ✅ archived | 2026-06-17 |
| H-2.5 (h2-5-architecture-foundation) | ✅ archived | 2026-06-19 |
| H-3 (h3-phase2-management) | ✅ archived | 2026-06-22 |
| H-3.5 | h3-5-followup-test-fixture-cleanup | ✅ archived | 2026-06-25 (5ff8c26 commit) |
| H-4 (h4-architecture-governance-cleanup) | 🔵 active | TBD |

---

## 四、当前架构

### 4.1 IGpuDriver 抽象层（H-2.5）

```
┌─────────────────────────────────────────────────────────────┐
│ IGpuDriver (28 methods)                                       │
│   ├── get_device_info / alloc_bo / submit_batch / wait_fence  │
│   ├── create_va_space / destroy_va_space / register_gpu        │
│   ├── create_queue / destroy_queue                            │
│   └── 详细规格见 ADR-032                                       │
└─────────────────────────────────────────────────────────────┘
            ▲                       ▲                   ▲
            │ DI 注入              │ DI 注入           │ 测试夹具
┌────────────┴──────────┐ ┌─────────┴──────────┐ ┌───────┴──────────┐
│ GpuDriverClient       │ │ CudaStub          │ │ MockGpuDriver   │
│ (真实 ioctl 实现)     │ │ (in-memory mock)  │ │ (测试夹具)      │
│ 通过 /dev/gpgpu0       │ │ monotonic handle  │ │ history()       │
│                      │ │ atomic + map 跟踪  │ │ inject_error()  │
└──────────────────────┘ └────────────────────┘ └──────────────────┘
```

### 4.2 Phase 2 lifecycle（D1-D5 决策）

详细决策见 ADR-033，关键点：
- **D1 caller owns**：`create_va_space()` 返回 u64 handle，**不**自动 set `current_va_space_handle_`
- **D2 explicit create-destroy**：`create_queue()` 返回 u64 queue_handle，caller 显式管理
- **D3 snake_case**：5 方法全部 snake_case（`create_va_space` 等）
- **D4 return only**：driver 不维护 handle metadata map（仅 mock 有 existence tracking）
- **D5 opt-in default**：构造时不自动 `create_va_space()`

### 4.3 R2 mapping contract

```
caller:                          driver:
  create_queue(...)              
    ↓                             ↑ monotonic from 1
  queue_handle (u64)             
    ↓                            
  (u32) stream_id = LOW32(handle)
    ↓                            
  submit_batch(stream_id, ...)    
    ↓                            
  driver 校验 static_cast<u64>(stream_id) 在 attached_queues 中
```

---

## 五、汇总

### 5.1 同步点完成率

| 同步点 | 状态 | 完成日期 |
|--------|------|----------|
| S0 | ✅ 完成 | 2026-04-28 |
| S1 | ✅ 完成 | 2026-04-28 |
| S2 | ✅ 完成 | 2026-04-28 |
| S3 | ✅ 完成 | 2026-04-28 |
| S3.5 | ✅ 完成 | 2026-05-13 |
| S5 | ✅ 完成 | 2026-06-19 |
| **Phase 3.1+3.2 Step 1** | ✅ 完成 | **2026-07-06 (commit e6a34eb)** |
| **Phase 3.1+3.2 Step 3** | ✅ 完成 | **2026-07-07 (branch phase3-step3-shim-and-forwarding, 7 atomic commits)** |
| **Phase 4 real-impl-bridge** | 🔄 进行中 | 2026-07-07 |

### 5.2 测试基线

| 测试 | 状态 | 备注 |
|------|------|------|
| test_cuda_scheduler | ✅ 8/8 | H-1 baseline preserved |
| test_gpu_phase2 | ✅ 12/12 | H-3 新增 |
| test_gpu_architecture | ✅ 11/11 | H-2.5 baseline + Phase 3.1 fix |
| test_cuda_shim | ✅ 103/103 | Phase 1.7 (commit defd272) |
| test_cuda_runtime_api | ✅ 8/8 | Phase 1 CudaRuntimeApi |
| test_cu_stream_capture | ✅ 30/30 | Phase 3.1 (Step 3) NEW |
| test_cu_graph | ✅ 25/25 | Phase 3.1 (Step 3) NEW |
| test_cu_mem_pool | ✅ 28/28 | Phase 3.2 (Step 3) NEW |
| test_cu_mem_pool_export | ✅ 13/13 | Phase 4 (real-impl-bridge) NEW |
| test_cu_graph_real | ✅ 32/32 | Phase 4 (cuGraphLaunch REAL bridge) NEW |
| **总计** | **270/270** | **+45 new + 225 baseline, 0 failures** |
| UsrLinuxEmu docs-audit | ✅ 53/53 | pre-commit hook (Phase 1.7 后) |

### 5.3 下一波 change 候选

| 候选 | 来源 | 工时 | 前置 TADR | 状态 |
|------|------|---:|----------|------|
| **~~H-3.5~~** | CudaStub guard verification | ✅ 完成 (2026-06-25, 5ff8c26) | TADR-006 | Done |
| **~~H-7 ADR~~** | 3 upstream issues | ✅ 全部完成 (H-3.6/3.7/3.8) | TADR-008 | Done |
| **Phase 3.1+3.2 Step 1** | IGpuDriver 31→46 扩展 | ✅ 完成 (2026-07-06, e6a34eb) | TADR-301 | Done |
| **Phase 3.1+3.2 Step 2** | UsrLinuxEmu sim primitives + 18 IOCTL | ✅ 完成 (2026-07-06, 138f15a, PR #20 merged) | ADR-015 | Done |
| **Phase 3.1+3.2 Step 3** | GpuDriverClient 15 forwarding + shim + E2E | ✅ 完成 (2026-07-07, 7 atomic commits, 225/225 tests) | TADR-301 | Done |
| **Phase 3.1+3.2 Step 4** | UsrLinuxEmu submodule bump | ⏳ 待 Step 3 merge | ADR-035 | 待 PR merge |
| **Phase 1.7 test coverage** | 25-30 E2E tests (REAL_IMPL 50.5%→≥85%) | 🟢 可立即开始 (独立) | — | PROPOSED |
| **Phase 3.3 Event+Texture** | Frontend-only (cuEvent + cuTexRef) | 🟢 可立即开始 (独立) | — | DRAFT plan |

### 5.4 Phase 3 跨仓协调时间线

```
2026-07-04  Stage 1.4 完成 (80f6a44 + 9378153) → 触发 Phase 3.1+3.2 kickoff
2026-07-05  UsrLinuxEmu openspec ACCEPTED + 11 项 BLOCKER/MUST-FIX 决议
2026-07-05  UsrLinuxEmu PR #20 创建 (bd51dc9, 49 tests pass, 81/81 zero-regression)
2026-07-06  TaskRunner Step 1 完成 (e6a34eb) ✅
2026-07-06  UsrLinuxEmu PR #20 merged (138f15a, 36 处 IOCTL 引用) ✅
2026-07-07  TaskRunner Step 3 完成 (phase3-step3-shim-and-forwarding, 7 atomic commits, 225/225 tests) ✅ ← archived 2026-07-07 (76f14e0)
2026-07-15  🎯 Step 3 PR merge 截止 (TaskRunner owner) → ✅ done 2026-07-07 (PR #7 merge commit 02363b8)
2026-07-22  🎯 Step 4 submodule bump 截止 (UsrLinuxEmu owner) → ✅ done 2026-07-07 (UsrLinuxEmu commit 458299e)
2026-07-25  🎯 最终回归 + openspec archive → 🟡 partial: openspec archive done 2026-07-07 (TaskRunner 76f14e0); final regression 待评估
```

### 5.5 决策追踪（D-SC-* / D-MP-*）

| 编号 | 决策 | 来源 | 状态 |
|------|------|------|------|
| D-SC-5 | Capture mode 仅接受 GLOBAL | F-1 (UsrLinuxEmu) | ✅ |
| D-SC-9 | GpuQueueEmu API 集成路径 (`submit(uint64_t, uint32_t)`) | B-1 (UsrLinuxEmu) | ✅ |
| D-SC-11 | fence_id 范围划分 (HAL [1, 1<<32-1] + sim [1<<32, INT64_MAX]) | B-3 (UsrLinuxEmu) | ✅ |
| D-SC-12 | kernargs_bo_handle=0 语义 | F-3 (UsrLinuxEmu) | ✅ |
| D-MP-1 | Pool VA 范围 Option B (VA 子范围预留) | B-2 (UsrLinuxEmu) | ✅ |

---

## 六、沟通机制

### 6.1 同步点触发流程

```
1. 触发方提前 3 天发送 "同步点预警"
   └── 包含: 问题列表、期望答案、截止时间

2. 接收方在截止时间前回复
   └── 如超时，触发方有权按最优假设继续

3. 触发方确认收到并开始执行
   └── 回复: "已收到，开始执行"
```

### 6.2 治理规则

ADR 治理政策见 ADR-035。openspec change 流程：
- 提案（🔵 PROPOSED）→ 实施（🟢 ACTIVE）→ 归档（📦 ARCHIVED）
- 每个 change 含 5 文件（proposal.md / design.md / tasks.md / spec.md / .openspec.yaml）
- archive policy 详见 ADR-035

---

## 七、风险与缓解

| 风险 | 影响 | 缓解 |
|------|------|------|
| H-3 已 shippable 但 MockGpuDriver guard 不实现 | 测试覆盖偏差 | H-3.5 follow-up（CudaStub guard tests）|
| H-7 ADR 3 个 upstream issue | 静默 -EINVAL / 类型溢出 / 行为分歧 | ADR-034 已注册，推迟到 Phase 3 owner 触发 |
| 跨仓 sync 失败 | submodule 指针错位 | 遵循 TaskRunner 先 push → UsrLinuxEmu combined commit 流程 |

---

**最后更新**: 2026-07-07（Phase 4 real-impl-bridge, v2.4 新增 §1.5 + tadr-301/302）
**下次审查**: Phase 4 submodule bump 时