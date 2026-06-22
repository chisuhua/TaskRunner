# TaskRunner-UsrLinuxEmu 接口统一同步计划

**版本**: v2.0（H-4 governance cleanup 后精简版）
**日期**: 2026-06-23
**维护者**: UsrLinuxEmu Architecture Team + TaskRunner owner
**前置**: H-2.5 ✅ + H-3 ✅ shippable（2026-06-23）

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

---

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

## 三、Phase 1.5 和 Phase 2 已完成

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

### 5.2 测试基线

| 测试 | 状态 | 备注 |
|------|------|------|
| test_cuda_scheduler | ✅ 8/8 | H-1 baseline preserved |
| test_gpu_phase2 | ✅ 12/12 | H-3 新增 |
| test_gpu_architecture | ⚠️ 10/11 | H-2.5 Bonus 预存在 baseline |
| UsrLinuxEmu docs-audit | ✅ 36/36 | pre-commit hook |

### 5.3 下一波 change 候选

| 候选 | 来源 | 工时 |
|------|------|---:|
| **H-3.5** | CudaStub guard verification（关闭 H-3 T6-T9 mock-behavior deviation）| 0.5 天 |
| **H-7 ADR** | 修复 3 个 owner-flagged upstream issue（stream_id u32 / ioctl 绕过 / attached_queues 弱校验）| 1-2 周 |
| **Phase 3** | Multi-GPU / P2P（需要先完成 H-7 ADR）| 3-4 周 |

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

**最后更新**: 2026-06-23（H-4 governance cleanup, v2.0 精简版）
**下次审查**: H-3.5 启动时