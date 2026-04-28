# TaskRunner-UsrLinuxEmu 接口统一实施计划

**版本**: v1.0
**日期**: 2026-04-28
**状态**: 已通过 ADR-015 评审
**维护者**: UsrLinuxEmu Architecture Team + TaskRunner Team

---

## 一、协调工作流建议

### 1.1 核心原则

```
1. 接口定义 (Canonical Source)
   └── UsrLinuxEmu 定义 gpu_ioctl.h，是唯一真源

2. 双向依赖，同步点驱动
   └── UsrLinuxEmu 实现驱动，TaskRunner 实现客户端
   └── 在定义的同步点等待对方输入，其他时间并行开发

3. 契约优先 (Contract-First)
   └── ADR-015 已定义接口契约，双方独立实现
   └── 通过 headless 测试验证契约一致性
```

### 1.2 推荐工作流：同步门限法 (Sync-Gate Workflow)

```
UsrLinuxEmu 团队                          TaskRunner 团队
──────────────                            ──────────────

[Phase 0]                                 [Phase 0]
    │                                         │
    ├── 废弃 cuda_compat_ioctl.cpp ──────────▶│ 确认符号链接方向
    │      (等待 TaskRunner 确认)              │
    │                                         │
[Phase 1.1: GPU_IOCTL 实现]                 [Phase 1.2: GpuDriverClient 实现]
    │  (并行，无依赖)                           │  (使用 mock/stub，暂不调用真驱动)
    │                                         │
    ▼                                         ▼
[同步点 S1: GET_DEVICE_INFO]◀───────────────│ 询问: GET_DEVICE_INFO 返回结构体
    │  (回应 TaskRunner 的问题)                │  开始实现版本协商
    │                                         │
    ▼                                         ▼
[实现 ALLOC_BO/FREE_BO]                      [实现 cuda_alloc/cuda_free]
    │  (并行，无依赖)                           │  (使用 mock return values)
    │                                         │
    ▼                                         ▼
[同步点 S2: ALLOC_BO 签名确认]◀──────────────│ 询问: domain 参数取值？handle 范围？
    │  (回应 TaskRunner 的问题)                │  开始内存管理实现
    │                                         │
    ▼                                         ▼
[实现 MAP_BO/PUSHBUFFER_SUBMIT_BATCH]        [实现 cuda_memcpy/cuda_launch]
    │  (并行，无依赖)                           │
    │                                         │
    ▼                                         ▼
[同步点 S3: PUSHBUFFER_SUBMIT_BATCH 格式]◀──│ 询问: entries 数组格式？fence 返回位置？
    │  (回应 TaskRunner 的问题)                │  开始命令封装
    │                                         │
    ▼                                         ▼
[实现 WAIT_FENCE]                            [实现 cuda_wait]
    │  (并行，无依赖)                           │
    │                                         │
    ▼                                         ▼
[同步点 S4: 端到端集成验证]◀──────────────────│ 提交集成测试请求
    │  (运行 headless 测试)                    │  提供测试用例
    │                                         │
[Phase 2]                                    [Phase 2]
    │  (根据 TaskRunner 需求)                  │
    ├── VA Space 抽象设计 ────────────────────▶│ 反馈: TaskRunner 需要哪些 VA 操作？
    │      (等待 TaskRunner 输入)              │
    │                                         │
    ├── Queue 抽象设计 ──────────────────────▶│ 反馈: TaskRunner 需要哪些队列类型？
    │      (等待 TaskRunner 输入)              │
    │                                         │
[Phase 3]                                    [Phase 3]
    │  (清理)                                  │  (清理)
```

### 1.3 同步点定义

| 同步点 | 触发条件 | 等待方 | 需提供的信息 |
|--------|----------|--------|-------------|
| **S0** | Phase 0 准备废弃旧文件 | UsrLinuxEmu | 确认符号链接方向；确认废弃清单 |
| **S1** | GET_DEVICE_INFO 实现前 | TaskRunner | 询问版本协商流程；需要哪些 device 属性 |
| **S2** | ALLOC_BO 实现前 | TaskRunner | 询问 domain 参数取值；handle 范围 |
| **S3** | PUSHBUFFER_SUBMIT_BATCH 实现前 | TaskRunner | 询问 entries 格式；fence 返回位置 |
| **S4** | Phase 1 完成 | 双方 | headless 测试用例；验证结果 |
| **S5** | VA Space/Queue 抽象设计前 | TaskRunner | 反馈真实驱动使用场景 |

---

## 二、Phase 0: 架构决策 + 补充定义 (Week 1)

**目标**: 确立 System C 为 canonical 接口，废弃旧体系 A/B

### 2.1 UsrLinuxEmu 侧任务

| 状态 | 任务 | 依赖 | 同步点 |
|------|------|------|--------|
| ✅ 已完成 | `GPU_IOCTL_WAIT_FENCE` 已定义 (gpu_ioctl.h:137) | - | - |
| ✅ 已完成 | 符号链接方向确认 (Issue #8 回复) | - | S0 |
| ✅ 已完成 | 在 `ioctl_gpgpu.h` 添加 `[[deprecated]]` | - | S0 |
| ✅ 已完成 | 在 `cuda_ioctl.h` 添加 `[[deprecated]]` | - | S0 |
| ✅ 已完成 | 从 CMakeLists.txt 移除 `cuda_compat_ioctl.cpp` | - | S0 |
| ⬜ 待执行 | 废弃 `CudaStub` | 已无依赖 | - |
| ⬜ 待执行 | 修正 `gpu_driver_architecture.md` 符号链接说明 | - | - |
| ⬜ 待执行 | Master Plan 插入里程碑 3.0 (P0 快速通道) | - | - |
| ⚠️ 暂缓 | 删除 `ioctl_gpgpu.h` | 10 个测试/驱动文件仍使用 | - |

### 2.2 S0 结果 (2026-04-28)

> ✅ **阻塞解除** — Issue #8 确认完成

**Q0-1: 符号链接方向** — ✅ 确认方向正确
**Q0-2: 废弃清单** — ⚠️ `ioctl_gpgpu.h` 暂缓删除（10 个文件依赖）

---

## 二.5、Phase 1: System C 必需功能实现 (Week 2-4)

**目标**: 实现 P0 ioctl 命令，连通 TaskRunner 到 GPU 的基本路径

### 3.1 UsrLinuxEmu 侧任务

#### 3.1.1 接口定义 (已完成 ✅)

| 命令 | 位置 | 状态 |
|------|------|------|
| `GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH` | gpu_ioctl.h:38 | ✅ |
| `GPU_IOCTL_ALLOC_BO` | gpu_ioctl.h:91 | ✅ |
| `GPU_IOCTL_FREE_BO` | gpu_ioctl.h:109 | ✅ |
| `GPU_IOCTL_MAP_BO` | gpu_ioctl.h:115 | ✅ |
| `GPU_IOCTL_WAIT_FENCE` | gpu_ioctl.h:137 | ✅ |
| `GPU_IOCTL_GET_DEVICE_INFO` | gpu_ioctl.h:220 | ✅ |

#### 3.1.2 GpuDriver 实现

| 状态 | 任务 | 依赖 | 同步点 |
|------|------|------|--------|
| ⬜ 待实现 | 在 `gpu_driver.cpp` 新增 System C 分支 (switch case `'G'`) | Phase 0 | - |
| ✅ 已完成 | 实现 `GPU_IOCTL_GET_DEVICE_INFO` | S1 双方确认 | **S1** |
| ⬜ 待实现 | 实现 `GPU_IOCTL_ALLOC_BO` (支持 domain) | S1 | **S2** |
| ⬜ 待实现 | 实现 `GPU_IOCTL_FREE_BO` | S2 | - |
| ⬜ 待实现 | 实现 `GPU_IOCTL_MAP_BO` | S2 | - |
| ⬜ 待实现 | 实现 `GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH` | S2 | **S3** |
| ⬜ 待实现 | 实现 `GPU_IOCTL_WAIT_FENCE` | S3 | - |

#### 3.1.3 需要 TaskRunner 反馈

> ⚠️ **UsrLinuxEmu 团队在以下点需等待 TaskRunner 输入**

| 同步点 | 问题 | 需要 TaskRunner 回答 |
|--------|------|---------------------|
| ~~**S1**~~ | ~~GET_DEVICE_INFO 返回哪些字段？~~ | ✅ **已完成** (Issue #9) |
| **S2** | ALLOC_BO domain/handle/flags/gpu_va | ✅ **已完成** (Issue #3) |
| **S3** | PUSHBUFFER entries 格式确认 | ✅ **已完成** (Issue #4) |
| **S3.5** | fence_id 返回机制扩展 | ⚠️ Phase 1.5 结构体扩展 |

### 2.5.3 S3 结果 (2026-04-28)

> ✅ **大部分阻塞解除** — Q3-1, Q3-2, Q3-4 已确认

**Q3-1 MEMCPY 映射**:
- method: ✅ GPU_OP_MEMCPY (0x102)
- payload[0]=src, payload[1]=dst, payload[2]=size, payload[3-6]=0
- semaphore: ⚠️ Phase 1 忽略，填 0

**Q3-2 LAUNCH_KERNEL 映射**:
- method: ✅ GPU_OP_LAUNCH_KERNEL (0x100)
- payload[0]=内核索引, payload[1]=grid_dim, payload[2]=block_dim
- kernel_name 通过预注册内核表索引传递

**Q3-3 fence 返回机制**:
- ⚠️ 需要结构体扩展: `gpu_pushbuffer_args` 增加 `fence_id` 字段
- Phase 1.5 实施，或 Phase 1 采用简化方案

**Q3-4 entry_count=1**:
- ✅ 确认正确

---

## 三、Phase 1 同步点完成状态

| 状态 | 任务 | 依赖 | 同步点 |
|------|------|------|--------|
| ⬜ 待实现 | 实现 `GpuDriverClient` 封装层 | Phase 0 | - |
| ⬜ 待实现 | 版本协商 (GET_DEVICE_INFO) | UsrLinuxEmu 实现 S1 后 | S1 |
| ⬜ 待实现 | 实现 cuda_alloc/cuda_free | UsrLinuxEmu 实现 S2 后 | S2 |
| ⬜ 待实现 | 实现 cuda_memcpy/cuda_launch | UsrLinuxEmu 实现 S3 后 | S3 |
| ⬜ 待实现 | 实现 cuda_wait | UsrLinuxEmu 实现 WAIT_FENCE | - |
| ⬜ 待验证 | 端到端集成测试 | UsrLinuxEmu 完成 S4 | **S4** |

---

## 四、Phase 2: System C 扩展抽象 (Week 5-8)

**目标**: 引入 VA Space 和 Queue 抽象，支持多 GPU 和复杂场景

### 4.1 UsrLinuxEmu 侧任务

#### 4.1.1 新增接口定义

| 状态 | 任务 | 依赖 | 同步点 |
|------|------|------|--------|
| ⬜ 待定义 | `GPU_IOCTL_CREATE_VA_SPACE` | - | **S5** |
| ⬜ 待定义 | `GPU_IOCTL_DESTROY_VA_SPACE` | - | - |
| ⬜ 待定义 | `GPU_IOCTL_REGISTER_GPU` | - | **S5** |
| ⬜ 待定义 | `GPU_IOCTL_CREATE_QUEUE` | - | **S5** |
| ⬜ 待定义 | `GPU_IOCTL_DESTROY_QUEUE` | - | - |

#### 4.1.2 实现里程碑

| 状态 | 里程碑 | 依赖 | 同步点 |
|------|--------|------|--------|
| ⬜ 待实现 | 里程碑 3.1: `drm_driver.cpp` 完整实现 | Phase 1 | - |
| ⬜ 待实现 | 里程碑 3.2: `mmu_event_dispatcher.cpp` + `tlb_emu.cpp` | 里程碑 3.1 | - |
| ⬜ 待实现 | 里程碑 3.3: `hardware_puller_emu.cpp` + `pcie_bus_emu.cpp` | 里程碑 3.2 | - |
| ⬜ 待实现 | 里程碑 3.4: MMU 事件回调 (`GPU_IOCTL_REGISTER_MMU_EVENT_CB`) | 里程碑 3.3 | - |
| ⬜ 待实现 | 里程碑 3.5: 固件回调 (`GPU_IOCTL_REGISTER_FIRMWARE_CB`) | 里程碑 3.4 | - |

#### 4.1.3 需要 TaskRunner 反馈

> ⚠️ **UsrLinuxEmu 团队在以下点需等待 TaskRunner 输入**

| 同步点 | 问题 | 需要 TaskRunner 回答 |
|--------|------|---------------------|
| **S5** | TaskRunner 是否需要 VA Space 抽象？ | 单 GPU 场景可能不需要 |
| **S5** | TaskRunner 需要哪些队列类型？ | COMPUTE/COPY/GRAPHICS |
| **S5** | 多 GPU/peer-to-peer 场景需求？ | 确认是否需要 REGISTER_GPU |

### 4.2 TaskRunner 侧任务 (参考)

| 状态 | 任务 | 依赖 | 同步点 |
|------|------|------|--------|
| ⬜ 待确认 | VA Space 抽象是否必需？ | UsrLinuxEmu 设计 S5 后 | **S5** |
| ⬜ 待确认 | Queue 类型需求？ | UsrLinuxEmu 设计 S5 后 | **S5** |
| ⬜ 待实现 | 固件回调接收 | 里程碑 3.5 | - |
| ⬜ 待实现 | MMU 事件处理 | 里程碑 3.4 | - |
| ⬜ 待实现 | 端到端测试 `test_cpu_gpu_task_fork.cpp` | Phase 2 | - |

---

## 五、Phase 3: 清理与验证 (Week 9-12)

**目标**: 清理废弃代码，验证零耦合

### 5.1 UsrLinuxEmu 侧任务

| 状态 | 任务 | 验证方式 |
|------|------|----------|
| ⬜ 待执行 | 删除 `cuda_compat_ioctl.cpp` | 文件不存在 |
| ⬜ 待执行 | 删除 `CudaStub` 相关代码 | 无引用 |
| ⬜ 待执行 | 运行 `test_portability.sh` | 行为一致性验证 |
| ⬜ 待执行 | 运行 `verify_symlinks.sh` | 符号链接验证 |
| ⬜ 待验证 | `ldd libgpu_plugin.so` | 确认无 TaskRunner 二进制依赖 |

### 5.2 TaskRunner 侧任务 (参考)

| 状态 | 任务 |
|------|------|
| ⬜ 待执行 | 清理废弃的 CUDA_IOCTL_* 调用 |
| ⬜ 待验证 | CMake 检查 symlink 断裂则 FATAL_ERROR |

---

## 六、汇总统计

### 6.1 UsrLinuxEmu 侧

| Phase | 总任务数 | 已完成 | 待处理 | 需同步点 |
|-------|----------|--------|--------|----------|
| Phase 0 | 7 | 1 | 6 | 1 (S0) |
| Phase 1 (定义) | 6 | 6 | 0 | 0 |
| Phase 1 (实现) | 7 | 0 | 7 | 3 (S1,S2,S3) |
| Phase 2 (定义) | 5 | 0 | 5 | 1 (S5) |
| Phase 2 (实现) | 5 | 0 | 5 | 0 |
| Phase 3 | 5 | 0 | 5 | 0 |
| **总计** | **35** | **7** | **28** | **5** |

### 6.2 同步点汇总

| 同步点 | 阶段 | 等待方 | 关键问题数 | 阻塞任务数 |
|--------|------|--------|------------|------------|
| S0 | Phase 0 | UsrLinuxEmu | 2 | 3 |
| S1 | Phase 1 | UsrLinuxEmu | 1 | 1 |
| S2 | Phase 1 | UsrLinuxEmu | 2 | 3 |
| S3 | Phase 1 | UsrLinuxEmu | 2 | 2 |
| S4 | Phase 1 末 | 双方 | - | - |
| S5 | Phase 2 | UsrLinuxEmu | 3 | 5 |

---

## 七、沟通机制

### 7.1 同步点触发流程

```
1. 触发方提前 3 天发送 "同步点预警"
   └── 包含: 问题列表、期望答案、截止时间

2. 接收方在截止时间前回复
   └── 如超时，触发方有权按最优假设继续

3. 触发方确认收到并开始执行
   └── 回复: "已收到，开始执行"
```

### 7.2 Headless 测试机制

```
UsrLinuxEmu 实现 ioctl 命令
    │
    ├──▶ 提供 mock driver (用于 TaskRunner 独立测试)
    │
TaskRunner 提交 headless 测试用例
    │
    ├──▶ 测试用例: 预期输入/输出，不依赖真实 GPU
    │
UsrLinuxEmu 运行测试
    │
    └──▶ 结果: PASS/FAIL + 失败原因
```

### 7.3 Issue 跟踪

| 同步点 | GitHub Issue | 状态 |
|--------|-------------|------|
| S0 | 待创建 | Open |
| S1-S3 | 待合并到单一 Issue | Open |
| S4 | Integration Testing | Open |
| S5 | Phase 2 Design | Open |

---

## 八、风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| TaskRunner 响应延迟 | UsrLinuxEmu 在 S1-S3,S5 阻塞 | 3 天超时机制，按最优假设继续 |
| headless 测试覆盖不足 | 集成时发现契约不一致 | 每个 S 点要求提供测试用例 |
| Phase 2 需求变更 | VA Space/Queue 抽象不合适 | S5 充分讨论，按需裁剪 |
| 双项目分支管理 | 代码合并冲突 | 统一 commit 规范，定期 sync |

---

**最后更新**: 2026-04-28
**下次审查**: 每次同步点完成后
