---
SCOPE: TEST-FIXTURE
STATUS: ACCEPTED
---

# TaskRunner 分层架构视图

> **最后更新**: 2026-06-23（H-4.5 docs governance cleanup）
> **关联**: [current.md](./current.md), UsrLinuxEmu [`docs/02_architecture/post-refactor-architecture.md`](../../../docs/02_architecture/post-refactor-architecture.md)

## 5 层结构

```
┌────────────────────────────────────────────────────────────────┐
│ Layer 5: Application (用户应用)                                  │
│   - CUDA App: vectorAdd.cu, matrixMul.cu                        │
│   - Vulkan App: compute_shader.cpp                              │
│   - 职责: 用户代码，无需了解 GPU 驱动细节                        │
└────────────────────────────────────────────────────────────────┘
                              ▼
┌────────────────────────────────────────────────────────────────┐
│ Layer 4: Runtime Stub (用户态 API 兼容层)                        │
│   - cuda_stub.cpp (libcuda.so 兼容)                             │
│   - vk_compute_stub.cpp (Phase 3+ libvulkan.so 兼容)           │
│   - 职责: 提供 CUDA Runtime API + Vulkan Compute API 用户态接口 │
└────────────────────────────────────────────────────────────────┘
                              ▼
┌────────────────────────────────────────────────────────────────┐
│ Layer 3: Scheduling (TaskRunner 调度层)                          │
│   - CudaScheduler (持有 IGpuDriver* via DI)                    │
│   - CudaResult / LaunchParams (数据类型)                        │
│   - 职责: 命令编码 + 资源分配 + 跨 API 互操作                    │
└────────────────────────────────────────────────────────────────┘
                              ▼
┌────────────────────────────────────────────────────────────────┐
│ Layer 2: Abstraction (TaskRunner 用户态 GPU 驱动契约)           │
│   - IGpuDriver (28 虚方法, 311 行)                             │
│   - 3 实现: GpuDriverClient / CudaStub / MockGpuDriver        │
│   - 职责: 抽象 GPU 驱动操作，跨实现可替换                        │
└────────────────────────────────────────────────────────────────┘
                              ▼
┌────────────────────────────────────────────────────────────────┐
│ Layer 1: Driver Backend (UsrLinuxEmu 内核侧驱动模拟)            │
│   - UsrLinuxEmu GPU Driver Plugin (/dev/gpgpu0)                │
│   - VA Space + Queue + Ring Buffer + Doorbell                  │
│   - 职责: GPU 内核侧命令执行 (含用户态模拟)                     │
└────────────────────────────────────────────────────────────────┘
```

## 跨层接口

| 上层 | 下层 | 接口 |
|------|------|------|
| Layer 5 (App) | Layer 4 (Stub) | CUDA Runtime API (cudaMalloc / cudaLaunchKernel / ...) |
| Layer 4 (Stub) | Layer 3 (Scheduler) | C++ 类方法 (CudaScheduler::submit / CudaResult) |
| Layer 3 (Scheduler) | Layer 2 (Abstraction) | C++ 虚方法 (IGpuDriver::submit_batch 等 28 方法) |
| Layer 2 (Abstraction) | Layer 1 (Backend) | ioctl (`GPU_IOCTL_*`) + `/dev/gpgpu0` |

## 命名空间映射

| 层 | 命名空间 | 说明 |
|----|---------|------|
| Layer 4 (Stub) | `async_task::gpu::CudaStub` | 旧 `taskrunner::CudaStub` 兼容 1 release（H-2.5 §D9） |
| Layer 3 (Scheduler) | `async_task::gpu::CudaScheduler` | |
| Layer 2 (Abstraction) | `async_task::gpu::IGpuDriver` + 3 实现 | |
| Layer 1 (Backend) | `UsrLinuxEmu::*` | 不同仓，跨仓 ioctl 通信 |

## Layer 2 内部结构（IGpuDriver 28 方法）

详见 [`include/igpu_driver.hpp`](../../include/igpu_driver.hpp)：

| 类别 | 方法数 | 方法 |
|------|------:|------|
| 核心生命周期 | 3 | `open` / `close` / `is_open` |
| FD 访问 | 1 | `fd` |
| 设备信息 | 8 | `get_device_info` / `get_warp_size` / `get_simd_count` / `get_peak_fp32_gflops` / `get_max_clock_frequency` / `get_driver_version_string` / `get_marketing_name` / `print_device_info` |
| Buffer Object | 4 | `alloc_bo` / `alloc_bo_vram` / `free_bo` / `map_bo` |
| 提交 | 3 | `submit_batch` / `submit_memcpy` / `submit_launch` |
| Fence 等待 | 2 | `wait_fence` (重载 × 2) |
| VA Space 透传 | 2 | `set_current_va_space` / `get_current_va_space` |
| Phase 2 lifecycle | 5 | `create_va_space` / `destroy_va_space` / `register_gpu` / `create_queue` / `destroy_queue` |
| **合计** | **28** | |

---

**END OF LAYERS**
