# TaskRunner-UsrLinuxEmu 接口统一实施计划

**版本**: v1.1
**日期**: 2026-04-29
**状态**: Phase 1 同步完成，UsrLinuxEmu 实现完成
**维护者**: UsrLinuxEmu Architecture Team + TaskRunner Team

---

## 一、协调工作流总结

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

### 1.2 同步门限法 (Sync-Gate Workflow) - 执行结果

```
UsrLinuxEmu 团队                          TaskRunner 团队
─────────────                            ──────────────

[Phase 0]                                 [Phase 0]
    │                                         │
    ├── 废弃 cuda_compat_ioctl.cpp ──────────▶│ ✅ S0 完成
    │                                         │
[Phase 1.1: GPU_IOCTL 实现]                 [Phase 1.2: GpuDriverClient 实现]
    │  (并行，无依赖)                           │  (使用 mock/stub，暂不调用真驱动)
    │                                         │
    ▼                                         ▼
[S1: GET_DEVICE_INFO]◀───────────────│ ✅ S1 完成
    │                                         │
    ▼                                         ▼
[ALLOC_BO/FREE_BO]                      [实现 cuda_alloc/cuda_free]
    │                                         │
    ▼                                         ▼
[S2: ALLOC_BO 签名确认]◀──────────────│ ✅ S2 完成
    │                                         │
    ▼                                         ▼
[MAP_BO/PUSHBUFFER_SUBMIT_BATCH]        [实现 cuda_memcpy/cuda_launch]
    │                                         │
    ▼                                         ▼
[S3: PUSHBUFFER_SUBMIT_BATCH 格式]◀──│ ✅ S3 完成
    │                                         │
    ▼                                         ▼
[WAIT_FENCE]                            [实现 cuda_wait]
    │                                         │
    ▼                                         ▼
[✅ S4: 端到端集成验证]◀──────────────────│ ⏳ 等待 UsrLinuxEmu 完成
    │  (UsrLinuxEmu 全部实现完成)            │  TaskRunner 准备联调
    │                                         │
[Phase 2]                                 [Phase 2]
```

---

## 二、GitHub Issue 追踪

### 2.1 TaskRunner 仓库 Issues

| Issue | 主题 | 状态 | 仓库 |
|-------|------|------|------|
| #3 | S2: ALLOC_BO 参数确认 | ✅ 已回复 | TaskRunner |
| #4 | S3: PUSHBUFFER 格式确认 | ✅ 已回复 | TaskRunner |
| #5 | Phase 1 实现清单 | ✅ UsrLinuxEmu 已完成 | TaskRunner |

### 2.2 UsrLinuxEmu 仓库 Issues

| Issue | 主题 | 状态 | 仓库 |
|-------|------|------|------|
| #8 | S0: 符号链接确认 | ✅ 已回复 | UsrLinuxEmu |
| #9 | S1: GET_DEVICE_INFO 确认 | ✅ 已回复 | UsrLinuxEmu |

---

## 三、Phase 0-1 同步点完成状态

### 3.1 S0: 符号链接方向确认 ✅

**Issue**: UsrLinuxEmu #8
**日期**: 2026-04-28
**结论**: 符号链接方向正确

| 确认项 | 结果 |
|--------|------|
| 符号链接方向 | ✅ `TaskRunner/UsrLinuxEmu → ../UsrLinuxEmu/plugins/gpu_driver/shared/` |
| 废弃清单 | ⚠️ `ioctl_gpgpu.h` 暂缓删除 (10 个文件依赖) |

### 3.2 S1: GET_DEVICE_INFO 实现确认 ✅

**Issue**: UsrLinuxEmu #9
**日期**: 2026-04-28

**确认的字段**:

| 字段 | TaskRunner 需求 | UsrLinuxEmu 实现 |
|------|-----------------|------------------|
| vendor_id | ✅ 需要 | ✅ 0x1000 |
| device_id | ✅ 需要 | ✅ 0x1001 |
| vram_size | ✅ 需要 | ✅ 8GB |
| bar0_size | ❌ 不需要 | ⚠️ 占位 16MB |
| max_channels | ✅ 需要 | ✅ 32 |
| compute_units | ✅ 需要 | ✅ 64 |
| gpfifo_capacity | ✅ 需要 | ✅ 1024 |
| cache_line_size | ⚠️ 可选 | ✅ 64 |

**Phase 1.5 已完成字段** (2026-05-13):

| 字段 | 类型 | 值 |
|------|------|-----|
| warp_size | u32 | 32 (NVIDIA 风格) |
| max_clock_frequency | u32 | 1500 MHz |
| driver_version | u32 | 0x000500 (v0.5.0) |
| firmware_version | u32 | 0x000100 (v0.1.0) |
| simd_count | u32 | 64 |
| max_memory_clock_frequency | u32 | 2000 MHz |
| memory_bus_width | u32 | 256-bit |
| peak_fp32_gflops | u32 | 17000 |
| pcie_bandwidth | u32 | 16000 Mbps |
| architecture_id | u32 | 0x1001 |
| marketing_name | char[64] | "UsrLinuxEmu Simulator v1" |

**struct 总大小**: 144 字节

### 3.3 S2: ALLOC_BO 参数确认 ✅

**Issue**: TaskRunner #3
**日期**: 2026-04-28

**确认的参数**:

| 参数 | 确认值 |
|------|--------|
| domain | VRAM(0x1)/GTT(0x2)/CPU(0x4)/多选组合 ✅ |
| handle | u32, 从 1 开始, 0-65535 ✅ |
| flags | DEVICE_LOCAL/HOST_VISIBLE ✅, CXL_SHARED 占位 |
| gpu_va | ✅ Phase 1 返回有效值 |

### 3.4 S3: PUSHBUFFER_SUBMIT_BATCH 格式确认 ✅

**Issue**: TaskRunner #4
**日期**: 2026-04-28

**S3.1 va_space_handle 透传**（2026-06-17, change `h1-pushbuffer-validation-closeout`）：✅ 已加 `setCurrentVASpace()` API（opt-in，默认 0 走 H-1 sentinel 跳过校验）。`GpuDriverClient::submit_batch()` 在 ioctl 前自动透传 `current_va_space_handle_` 到 `args.va_space_handle`。ABI 兼容（旧调用方零行为变化）。

**确认的 GPFIFO Entry 填充**:

**MEMCPY (h2d/d2h)**:
```c
entries[0].method = GPU_OP_MEMCPY;  // 0x102
entries[0].payload[0] = src_addr;
entries[0].payload[1] = dst_addr;
entries[0].payload[2] = size;
// semaphore 填 0 (Phase 1 忽略)
```

**LAUNCH_KERNEL**:
```c
entries[0].method = GPU_OP_LAUNCH_KERNEL;  // 0x100
entries[0].payload[0] = kernel_table_index;
entries[0].payload[1] = grid_x | (grid_y << 16) | (grid_z << 24);
entries[0].payload[2] = block_x | (block_y << 8) | (block_z << 16);
```

**fence 返回机制**: ⚠️ Phase 1.5 扩展 gpu_pushbuffer_args 增加 fence_id 字段

**entry_count=1**: ✅ 确认正确

### 3.5 S4: 端到端集成验证 ⏳

**状态**: UsrLinuxEmu Phase 1 实现已完成，TaskRunner 准备联调

---

## 四、UsrLinuxEmu Phase 1 实现状态 ✅

**Issue**: TaskRunner #5 回复
**日期**: 2026-04-29

### 4.1 已完成的 6 个 ioctl 实现

| 命令 | 状态 | 实现详情 |
|------|------|----------|
| `GPU_IOCTL_GET_DEVICE_INFO` | ✅ 完成 | vendor_id=0x1000, device_id=0x1001, vram_size=8GB |
| `GPU_IOCTL_ALLOC_BO` | ✅ 完成 | Buddy Allocator, handle 1-65535 |
| `GPU_IOCTL_FREE_BO` | ✅ 完成 | handle 释放 |
| `GPU_IOCTL_MAP_BO` | ✅ 完成 | gpu_va 返回 |
| `GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH` | ✅ 完成 | 支持 GPU_OP_LAUNCH_KERNEL/MEMCPY/MEMSET/FENCE |
| `GPU_IOCTL_WAIT_FENCE` | ✅ 完成 | Phase 1 简化实现 (返回 status=1) |

### 4.2 实现位置

```
build/plugins/gpu_driver/
```

---

## 五、TaskRunner 侧下一步任务

### 5.1 Phase 1 联调准备

| 任务 | 状态 | 说明 |
|------|------|------|
| 重构 `cmd_cuda.cpp` 使用 GPU_IOCTL_* | ⏳ 待开始 | 替代 CUDA_IOCTL_* |
| 实现 GpuDriverClient 封装层 | ⏳ 待开始 | 4-5 个 System C 调用 |
| 符号链接验证 | ⏳ 待验证 | TaskRunner → UsrLinuxEmu/plugins/gpu_driver/shared |
| CMake 检查 symlink 断裂 | ⏳ 待添加 | FATAL_ERROR |

### 5.2 CUDA 命令映射 (待实现)

| 命令 | System C 调用 | 状态 |
|------|--------------|------|
| `cuda_alloc` | `GPU_IOCTL_ALLOC_BO` | ⏳ 待实现 |
| `cuda_free` | `GPU_IOCTL_FREE_BO` | ⏳ 待实现 |
| `cuda_memcpy h2d` | `GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH` | ⏳ 待实现 |
| `cuda_memcpy d2h` | `GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH` | ⏳ 待实现 |
| `cuda_launch` | `GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH` | ⏳ 待实现 |
| `cuda_wait` | `GPU_IOCTL_WAIT_FENCE` | ⏳ 待实现 |

---

## 六、Phase 1.5 和 Phase 2 待办

### 6.1 Phase 1.5

| 任务 | 状态 | 说明 |
|------|------|------|
| `gpu_pushbuffer_args.fence_id` 字段 | ✅ 已定义 (UsrLinuxEmu 2026-05-08) | S3.5 同步点已完成 |
| `gpu_device_info` 增加 warp_size 等字段 | ✅ 已完成 (2026-05-13) | struct 扩展到 144 字节，11 个新字段 |
| Issue #13: Teardown SIGSEGV 修复 | ✅ 已修复 (2026-05-09, commit dd81e5c) | plugin_fini 销毁顺序修复 |

### 6.2 Phase 2 (S5 已完成, 待 H-3 实施)

| 任务 | 状态 |
|------|------|
| `GPU_IOCTL_CREATE_VA_SPACE` | ✅ H-3 完成 (2026-06-23, commits 241f3ed..8625b82) |
| `GPU_IOCTL_CREATE_QUEUE` | ✅ H-3 完成 (2026-06-23, commits 241f3ed..8625b82) |
| VA Space/Queue 抽象设计 | ✅ S5 完成 (2026-06-19, commit c64301c) |

---

## 七、汇总统计

### 7.1 同步点完成率

| 同步点 | 阶段 | 状态 | 完成日期 |
|--------|------|------|----------|
| S0 | Phase 0 | ✅ 完成 | 2026-04-28 |
| S1 | Phase 1 | ✅ 完成 | 2026-04-28 |
| S2 | Phase 1 | ✅ 完成 | 2026-04-28 |
| S3 | Phase 1 | ✅ 完成 | 2026-04-28 |
| S3.5 | Phase 1.5 | ✅ 已完成 (2026-05-13) | fence_id 返回机制已实现 |
| S4 | Phase 1 | ⏳ 进行中 | - |
| S5 | Phase 1.5 架构基础 | ✅ 已完成 (2026-06-19) | IGpuDriver 抽象 + 2 实现 + DI + Mock + CLI 修复 (UsrLinuxEmu commit c64301c) |

### 7.2 UsrLinuxEmu 侧完成率

| Phase | 总任务数 | 已完成 | 待处理 |
|-------|----------|--------|--------|
| Phase 0 | 7 | 5 | 2 |
| Phase 1 (定义) | 6 | 6 | 0 |
| Phase 1 (实现) | 6 | 6 | 0 |
| Phase 1.5 | 3 | 3 | 0 |
| Phase 2 | 5 | 0 | 5 |
| **总计** | **27** | **19** | **8** |

---

## 八、沟通机制

### 8.1 同步点触发流程

```
1. 触发方提前 3 天发送 "同步点预警"
   └── 包含: 问题列表、期望答案、截止时间

2. 接收方在截止时间前回复
   └── 如超时，触发方有权按最优假设继续

3. 触发方确认收到并开始执行
   └── 回复: "已收到，开始执行"
```

### 8.2 Issue 跟踪

| 同步点 | GitHub Issue | 状态 |
|--------|-------------|------|
| S0 | UsrLinuxEmu #8 | ✅ 已完成 |
| S1 | UsrLinuxEmu #9 | ✅ 已完成 |
| S2 | TaskRunner #3 | ✅ 已完成 |
| S3 | TaskRunner #4 | ✅ 已完成 |
| S3.5 | 待创建 | ⏳ 待发起 |
| S4 | TaskRunner #5 | ✅ UsrLinuxEmu 已完成 |
| S5 | 待创建 | ⏳ Phase 2 前发起 |

---

## 九、风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| TaskRunner 联调延迟 | UsrLinuxEmu 等待 | 并行开发，3 天超时 |
| fence_id 扩展协调 | 需双方同步修改 | Phase 1.5 统一实施 |
| Phase 2 需求变更 | VA Space/Queue 抽象不合适 | S5 充分讨论 |

---

**最后更新**: 2026-04-29
**下次审查**: TaskRunner 联调开始前
