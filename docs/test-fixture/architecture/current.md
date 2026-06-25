---
SCOPE: TEST-FIXTURE
STATUS: ACCEPTED
---

# TaskRunner Current Architecture (Consumer-Lens)

> **最后更新**: 2026-06-23（H-4.5 docs governance cleanup）
> **关联**: [TADR-005 IGpuDriver 抽象层](../adr/tadr-005-h2-5-igpu-driver-consumer-lens.md) + UsrLinuxEmu [ADR-032](../../../../docs/00_adr/adr-032-h2-5-igpu-driver-abstraction.md)

TaskRunner 当前架构基于 **IGpuDriver 抽象 + 依赖注入** 模式，与 2026-04-07 v0.1 提案的 "UnifiedScheduler" 路径有偏差（详见 [TADR-001 实施路径备注](../adr/tadr-001-cuda-vulkan-runtime-unified-scheduler.md#实施路径备注) + [roadmap/retrospective.md](../roadmap/retrospective.md)）。

## 核心组件架构图

```
┌─────────────────────────────────────────────────────────────┐
│ Application Layer (Layer 5)                                  │
│   - CUDA App: vectorAdd.cu, matrixMul.cu                     │
│   - Vulkan App: compute_shader.cpp (Phase 3+)                │
└─────────────────────────────────────────────────────────────┘
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ Runtime Stub Layer (Layer 4)                                 │
│   - cuda_stub.cpp (libcuda.so 用户态兼容)                     │
│   - (vk_compute_stub.cpp — Phase 3+)                         │
└─────────────────────────────────────────────────────────────┘
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ Scheduling Layer (Layer 3) — TaskRunner                       │
│   - CudaScheduler (持有 IGpuDriver* via DI)                  │
│   - CudaResult / LaunchParams 数据类型                       │
└─────────────────────────────────────────────────────────────┘
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ Abstraction Layer (Layer 2) — IGpuDriver                      │
│   - 3 核心 (open / close / is_open)                          │
│   - 1 FD 访问 (fd)                                           │
│   - 8 设备信息 (get_device_info / get_warp_size / ...)       │
│   - 4 Buffer Object (alloc_bo / alloc_bo_vram / free_bo / map_bo) │
│   - 3 提交 (submit_batch / submit_memcpy / submit_launch)     │
│   - 2 Fence 等待 (wait_fence 重载)                            │
│   - 2 VA Space 透传 (set/get_current_va_space)               │
│   - 5 Phase 2 (create_va_space / destroy_va_space /          │
│                register_gpu / create_queue / destroy_queue)  │
│   - 接口定义: include/igpu_driver.hpp (28 虚方法, 311 行)    │
└─────────────────────────────────────────────────────────────┘
              ▲               ▲                ▲
              │ DI            │ DI             │ DI 测试夹具
┌─────────────┴──┐ ┌──────────┴────┐ ┌────────┴────────┐
│ GpuDriverClient│ │ CudaStub      │ │ MockGpuDriver  │
│ (真 ioctl)     │ │ (in-mem mock) │ │ (headless)     │
│ /dev/gpgpu0    │ │ atomic+map    │ │ history()      │
└────────┬───────┘ └───────────────┘ └─────────────────┘
         ▼
┌─────────────────────────────────────────────────────────────┐
│ Driver Backend (Layer 1) — UsrLinuxEmu                       │
│   GPU Driver Plugin (/dev/gpgpu0 ioctl)                      │
│   - VA Space + Queue + Ring Buffer + Doorbell                │
│   - 内核侧命令执行（含用户态模拟）                            │
└─────────────────────────────────────────────────────────────┘
```

## 关键设计决策（链接 TADR）

| 决策 | TADR | 上游 ADR |
|------|------|---------|
| IGpuDriver 抽象层 + 3 实现 | [TADR-005](../adr/tadr-005-h2-5-igpu-driver-consumer-lens.md) | UsrLinuxEmu [ADR-032](../../../../docs/00_adr/adr-032-h2-5-igpu-driver-abstraction.md) |
| 依赖注入 (CudaScheduler 接受 IGpuDriver*) | [TADR-005 §Consumer-Lens](../adr/tadr-005-h2-5-igpu-driver-consumer-lens.md#consumer-lens) | UsrLinuxEmu ADR-032 §D10 |
| CLI 死调用修复 (init_gpu_client 显式调用) | [TADR-005 §Consumer-Lens](../adr/tadr-005-h2-5-igpu-driver-consumer-lens.md#consumer-lens) | UsrLinuxEmu ADR-032 §D11 |
| 命名空间迁移 taskrunner:: → async_task::gpu:: | [TADR-005 §Consumer-Lens](../adr/tadr-005-h2-5-igpu-driver-consumer-lens.md#consumer-lens) | UsrLinuxEmu ADR-032 §D9 |
| Phase 2 5 方法 (create_va_space 等) | [TADR-006](../adr/tadr-006-h3-phase2-consumer-lens.md) | UsrLinuxEmu [ADR-033](../../../../docs/00_adr/adr-033-h3-phase2-lifecycle.md) |
| R2 mapping (LOW32 truncation) | [TADR-007](../adr/tadr-007-r2-mapping-contract.md) | UsrLinuxEmu ADR-033 §R2 |
| H-7 上游 issue 注册 | [TADR-008](../adr/tadr-008-h7-deferred-mirror.md) | UsrLinuxEmu [ADR-034](../../../../docs/00_adr/adr-034-h7-deferred-registry.md) |

## 实施期路径 deviation

实际实施路径与 v0.1 提案的关键差异：

- **原提案推荐**：UnifiedScheduler 中央调度器（TADR-001 B 方案）
- **实际实施**：IGpuDriver 抽象 + 3 实现 DI（CudaScheduler 仅持有 IGpuDriver*）
- **原因**：(1) 测试隔离需要 (2) 跨仓治理需要 (3) 命令编码灵活性
- **详见**：[roadmap/retrospective.md](../roadmap/retrospective.md)

## 测试基线（截至 2026-06-23）

| 测试 | 状态 | 来源 | 行/文件 |
|------|------|------|--------|
| `test_cuda_scheduler` | ✅ 8/8 | H-1 baseline preserved | tests/test_cuda_scheduler.cpp |
| `test_gpu_architecture` | ⚠️ 10/11 | H-2.5 Bonus 预存在 baseline | tests/test_gpu_architecture.cpp |
| `test_gpu_phase2` | ✅ 12/12 | H-3 新增 | tests/test_gpu_phase2.cpp |

---

**END OF CURRENT ARCHITECTURE**
