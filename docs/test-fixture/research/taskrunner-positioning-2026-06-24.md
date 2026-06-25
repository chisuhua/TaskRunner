---
SCOPE: TEST-FIXTURE
STATUS: ACCEPTED
DATE: 2026-06-24
RESEARCH_ID: bg_e34c1460
RESEARCH_TITLE: 分析 TaskRunner 自身定位与能力
RELATED_RESEARCH:
  - ../shared/research/usrlinuxemu-gpu-driver-design-2026-06-24.md
  - ../umd-evolution/research/external-nvidia-cuda-umd-2026-06-24.md
  - ../umd-evolution/research/external-amd-rocm-umd-2026-06-24.md
---

# TaskRunner 自身定位与能力边界分析

> **调研日期**：2026-06-24
> **调研目标**：澄清 TaskRunner 真实定位、与真实用户态驱动（UMD）的差距、演化空间
> **方法**：8 个 TADR + 5 份架构/路线图文档 + 12 份源代码 + 测试文件综合只读分析

## 执行摘要

1. **TaskRunner 是混合仓库**，包含两个独立代码体：原始 TaskRunner 调度框架（~1,225 LOC，GPU 无关）+ GPU 客户端层（~2,650 LOC，H-1~H-3 增量）
2. **CudaStub 是 mock，不是用户态驱动**：无 libcuda.so 链接、无 cu* 函数调用、所有 stub 方法直接返回 SUCCESS 不真实计算（`cuda_stub.cpp:30-44, 67-90, 112-130, 175-193`）
3. **GpuDriverClient 是唯一真实 ioctl**，但**只被 CLI 调用**；CudaScheduler 通过 `dynamic_cast<CudaStub*>` 走 stub 路径（`cuda_scheduler.cpp:100, 146, 187, 226, 268`）
4. **CudaScheduler 存在抽象泄漏**（动态类型转换紧耦合），IGpuDriver 抽象价值打折——注入 MockGpuDriver 后 legacy 路径会返回 `-ENOSYS`
5. **距离真实 libcuda.so 还需 6-12 月 + 50k-200k LOC 投入**，且性能/功能完整度 < NVIDIA 真驱动 10%

## 1. 两个并行代码体（仓库结构真相）

| 代码体 | 文件 | 真实职责 | LOC |
|--------|------|----------|----:|
| **原始 TaskRunner 调度框架** | `include/TaskRunner.h` (458 行) + `Barrier.h` (20) + `CmdBuffer.h` (192) + `CmdProcessor.h` (57) + `CmdStream.h` (68) + `EventQueue.h` (56) + `TaskQueue.h` (63) + `src/TaskRunner.cpp` (8) + `src/CmdProcessor.cpp` (194) + `sample/main.cpp` (109) | 通用 C++ 任务调度框架：单例 + CmdProcessor + EventQueue + Barrier（work-stealing），GPU 无关 | ~1,225 |
| **GPU 客户端层（H-1~H-3 增量）** | `include/cuda_stub.hpp` (226) + `cuda_scheduler.hpp` (221) + `igpu_driver.hpp` (311) + `gpu_driver_client.h` (576) + `src/cuda_stub.cpp` (506) + `cuda_scheduler.cpp` (369) + `gpu_driver_client.cpp` (34) + `cmd_cuda.cpp` (407) | UsrLinuxEmu `/dev/gpgpu0` ioctl 的 C++ 包装 + 28 方法抽象 + CLI 调试工具 | ~2,650 |

**关键判断**：原始 TaskRunner 框架本身只是个通用任务调度器（`sample/main.cpp` 演示了如何在多线程间派发 lambda，与 GPU 完全无关）。GPU 相关代码全部是后加的（2026-04~2026-06 共 ~3 个月），且几乎全部定位为"UsrLinuxEmu 的测试与调试基础设施"，而非生产级用户态驱动。

## 2. "cuda_stub" 的真相——它不是 Driver API 包装

**自相矛盾的证据**：

- `cuda_stub.hpp:5-7` 声称："DDS v1.2 架构定义 —— 封装 CUDA Driver API (cuMemAlloc, cuLaunchKernel, etc.)"
- 但 `cuda_stub.cpp` 实际是**彻底的 mock**：
  - `cuda_stub.cpp:30-44` `initialize()`：直接 `initialized_ = true; return SUCCESS;`
  - `cuda_stub.cpp:67-90` `mem_alloc(size, &ptr)`：stub 模式时 `*device_ptr = next_ptr.fetch_add(...)`；否则 `*device_ptr = 0x10000` 后返回 SUCCESS
  - `cuda_stub.cpp:112-130` `memcpy_h2d(...)`：stub 模式时**完全跳过内存拷贝**直接返回 SUCCESS
  - `cuda_stub.cpp:132-151` `memcpy_d2h(...)`：stub 模式时 `std::memset(dst, 0, size)`（用 0 填充）
  - `cuda_stub.cpp:175-193` `launch_kernel(...)`：递增 task_id 计数器后返回 SUCCESS（**不提交任何命令**）

**绝对证据**：

- `cuda_stub.cpp` **没有任何 `cu*` 函数调用**（grep `cu[A-Z][a-zA-Z]+` 仅命中"CUDA API 引用"注释）
- **没有 `dlopen("libcuda.so")` 或 `dlsym`**
- **没有 `cuda_runtime_api.h` / `cuda.h` include**
- 测试验证：`tests/test_cuda_scheduler.cpp:99` `CHECK(std::all_of(host_data.begin(), host_data.end(), [](uint8_t v) { return v == 0; }));` —— Stub 模式下 D2H 数据**确实是全 0**，证明 GPU 从未真实计算过

## 3. 真正的 GPU 通信——GpuDriverClient（生产路径）

唯一**真实**与 GPU "对话"的代码是 `gpu_driver_client.h:77-89`：

```cpp
int open() override {
    fd_ = ::open(device_path_.c_str(), O_RDWR | O_NOFOLLOW);  // 真 open /dev/gpgpu0
    ...
}

int64_t submit_batch(...) override {
    if (ioctl(fd_, GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH, &args) < 0) { ... }
    return static_cast<int64_t>(args.fence_id);  // 真 ioctl
}
```

但 `GpuDriverClient` 的调用者**只有 CLI（`cmd_cuda.cpp`）**。`CudaScheduler`（生产调度器）反而走的是 `dynamic_cast<CudaStub*>` 路径（见 `cuda_scheduler.cpp:100, 146, 187, 226, 268`）—— 即 scheduler 仍走 stub，与 GpuDriverClient **没有耦合**。

## 4. TaskRunner 定位综合判定

| 维度 | 判定 | 证据 |
|------|------|------|
| 是否用户态驱动 (UMD)？ | **否** | 无 libcuda.so 链接；无 cuModule/cuFunction；无 ELF/CUBIN 加载 |
| 是否 CUDA Runtime 替代？ | **否** | 无 cudaMalloc/cudaMemcpy/cudaLaunchKernel 导出符号 |
| 是否 Vulkan Stub？ | **否** | `vk_*` / `VK_*` 仅出现在归档 proposal，无实现 |
| 是否真实调度器？ | **是（仅 TaskRunner 原始框架）** | `TaskRunner.h` 是真调度器，但 `CudaScheduler` 是 thin wrapper |
| 是否测试夹具？ | **是** | `MockGpuDriver`（`tests/mock_gpu_driver.hpp`）+ `CudaStub`（mock 语义）显式定位 |
| 是否调试 CLI？ | **是** | `cmd_cuda.cpp` 6 个子命令（`cuda_alloc` / `cuda_memcpy` / `cuda_launch` / `cuda_wait` / `cuda_va_space` / `cuda_queue`） |
| 是否 ioctl 客户端？ | **是** | `GpuDriverClient` 是 `/dev/gpgpu0` 的 C++ 包装 |

## 5. TaskRunner ↔ UsrLinuxEmu 关系（架构分层）

```
Layer 5 (App):  CUDA App / Vulkan App (无 sample 代码，仅文档愿景)
   ↓
Layer 4 (Stub): cuda_stub.cpp (in-process mock) / vk_compute_stub.cpp (Phase 3+ ⏸️)
   ↓
Layer 3 (Scheduler): CudaScheduler 持有 IGpuDriver* via DI
   ↓
Layer 2 (Abstraction): IGpuDriver (28 虚方法, include/igpu_driver.hpp:311)
   ↓ [3 实现 DI]
   ├─ GpuDriverClient (真 ioctl, /dev/gpgpu0) ── 仅 CLI 调用
   ├─ CudaStub (in-memory mock)
   └─ MockGpuDriver (headless 测试夹具)
   ↓ ioctl GPU_IOCTL_*
Layer 1 (Backend): UsrLinuxEmu GPU Driver Plugin (gpgpu_device.cpp + BasicGpuSimulator)
```

### 5.1 双向关系判定

| 维度 | 判定 |
|------|------|
| **驱动方向** | UsrLinuxEmu **被驱动**，TaskRunner **驱动方** |
| **TaskRunner → UsrLinuxEmu** | ✅ 通过 5+ 个 `GPU_IOCTL_*` ioctl 单向调用（见 `gpu_driver_client.h:117-551`） |
| **UsrLinuxEmu → TaskRunner** | ❌ 无回调路径，无反向接口，无 mmap 共享内存 |
| **TaskRunner 是否"用户态驱动层"** | **部分是的，但有限定**：它是 "UsrLinuxEmu GPU 驱动模拟器的用户态测试/客户端层"，不是 "AMD/NVIDIA 用户态驱动" |

### 5.2 跨仓依赖单向性

- `TaskRunner → UsrLinuxEmu` 通过 ioctl（`GPU_IOCTL_*` 编号在 `plugins/gpu_driver/shared/gpu_ioctl.h`，通过 `UsrLinuxEmu` 符号链接访问）
- **TaskRunner 不依赖 UsrLinuxEmu 任何编译产物**（只 include 头文件）
- UsrLinuxEmu **完全不感知 TaskRunner 存在**
- 这是清晰的"驱动模拟器 ↔ 客户端"关系，而非"驱动 ↔ UMD"关系

## 6. "硬件相关操作"在 TaskRunner 中的实际体现

| 真实 GPU 驱动侧操作 | TaskRunner 中的位置 | 实现深度 |
|--------------------|---------------------|----------|
| **VA Space 创建/销毁** | `IGpuDriver::create_va_space/destroy_va_space`（3 实现覆盖） | ✅ 完整 ioctl wrapper（H-3 完整 4 项实施细节） |
| **GPU 绑定** | `IGpuDriver::register_gpu` | ✅ 完整 ioctl wrapper（`gpu_driver_client.h:473-490`） |
| **Queue 创建/销毁** | `IGpuDriver::create_queue/destroy_queue` | ✅ 完整 ioctl wrapper + R2 LOW32 truncation 显式化（TADR-104） |
| **Doorbell 写入** | ❌ **未实施** | `gpu_driver_client.h:531` 显式注释"doorbell_pgoff 由 ioctl 路径不需要（H-3 走 ioctl，非 mmap）" |
| **Ring Buffer 写入** | ❌ **未实施** | `submit_batch` 通过 `entries_addr` 指针传递，**仅引用不写** ring buffer |
| **GPFIFO entry 编码** | ✅ `gpu_driver_client.h:290-362` | 真实编码 `gpu_gpfifo_entry`，但只对 `submit_memcpy`/`submit_launch` 编码 |
| **BO 分配** | ✅ `alloc_bo/alloc_bo_vram` + `free_bo/map_bo` | 完整 ioctl wrapper，CudaStub mock 版只递增 handle 不真分配 |
| **Fence 等待** | ✅ `wait_fence` (2 重载) | 真实 ioctl (`GPU_IOCTL_WAIT_FENCE`)，mock 立即成功 |
| **Kernel 索引 (CUmodule 加载)** | ❌ **完全未实施** | `submit_launch` 用 `kernel_index: uint32_t`，无 CUmodule/CUfunction 加载逻辑 |
| **Register spilling / allocation** | ❌ **完全未实施** | TaskRunner 不解析任何 ISA |
| **Context 管理 (CUcontext)** | ⚠️ **部分** | `CudaStub::initialize/shutdown` 有 stub 痕迹，但无真实 context |

**结论**：TaskRunner 实现了**资源生命周期层**（VA Space / Queue / BO / Fence）和**命令编码层**（GPFIFO entry），但**完全未实现**ISA 加载（CUmodule）和 shader 执行（CUfunction launch）。后两者**完全在 UsrLinuxEmu 端**（`BasicGpuSimulator` 模拟）。

## 7. CudaStub 与 GpuDriverClient 的功能划分（关键洞察）

`cuda_scheduler.cpp:100, 146, 187, 226, 268` 全部使用 `dynamic_cast<async_task::gpu::CudaStub*>(driver_)`：

```cpp
// cuda_scheduler.cpp:100
auto* stub = dynamic_cast<async_task::gpu::CudaStub*>(driver_);
if (!stub) {
    memory_mgr_.free(mem);
    result.status = -ENOSYS;  // ← 如果 driver_ 是 GpuDriverClient，这里会失败
    return result;
}
```

这意味着 **`CudaScheduler` 的 legacy 路径实际上不通过 IGpuDriver 抽象**，而是通过 `dynamic_cast` 直接调用 CudaStub-specific 方法。**这是一个抽象泄漏 + 紧耦合问题**（未在任何 TADR 中识别）：

- 如果注入 `GpuDriverClient`，`submit_mem_alloc` 会返回 `-ENOSYS`
- 如果注入 `MockGpuDriver`，`submit_mem_alloc` 会**编译通过但 dynamic_cast 失败**返回 `-ENOSYS`
- 只有注入 `CudaStub` 才能工作

**这是当前架构的最大缺陷**，限制了 IGpuDriver 抽象的实际效用。

## 8. 与"真实用户态驱动 (libcuda.so)"的差距矩阵

| CUDA Runtime API | NVIDIA 语义 | TaskRunner 当前覆盖 | 差距 |
|-----------------|------------|---------------------|------|
| `cudaMalloc/cudaFree` | 分配/释放 device memory | ⚠️ 部分 | ❌ 无 `cudaMalloc` 导出；无 memory pool |
| `cudaMemcpy (H2D/D2H/D2D)` | 同步内存拷贝 | ⚠️ 部分 | ❌ 无 `cudaMemcpy` 导出 |
| `cudaMemcpyAsync` | 异步拷贝 | ❌ 无 | ❌ 全缺失 |
| `cudaStreamCreate/Destroy/Synchronize` | 流管理 | ❌ 无 | ❌ `stream_id` 仅作为 submit_batch 的 u32 参数 |
| `cudaLaunchKernel` | 启动 kernel | ⚠️ 部分 | ❌ 无 `CUfunction` 概念；kernel_name 是 string |
| `cuModuleLoad / cuModuleGetFunction` | 加载 CUBIN/PTX | ❌ **完全未实施** | ❌ 无 ELF parser；无 CUDA fatbin 解析 |
| `cuLaunchKernel` | 启动 CUfunction | ❌ 无 | ❌ 无 register spilling |
| `cudaEventCreate/Record/Wait` | 事件管理 | ⚠️ 部分 | ❌ 无 `cudaEvent` 导出 |
| `cudaDeviceSynchronize` | 全设备同步 | ❌ 无 | ❌ 无 device 概念 |
| `cudaSetDevice/GetDevice` | 多 GPU 切换 | ❌ 无 | ⏸️ Phase 3 deferred |
| `cuCtxCreate/Current/Destroy` | Context 管理 | ⚠️ 痕迹 | ❌ 无 `CUcontext` 概念 |
| `cudaMallocManaged` | Unified Memory | ❌ 无 | ❌ |
| `cudaGraph/Capture` | Graph 捕获 | ❌ 无 | ⏸️ Phase 1 proposal 提及但未实施 |

### 核心差距总结

1. **CUmodule/CUfunction 加载路径完全缺失**（最致命，占 libcuda.so 复杂度 ~40%）
2. **CUDA Runtime API 表面完全未导出**（无 ELF .so 符号，无法 LD_PRELOAD 或 dlopen）
3. **Stream object 模型缺失**（仅 stream_id u32）
4. **Context 模型缺失**（无 CUcontext）
5. **Unified Memory / VA Space mapping 缺失**（仅管理 VA Space handle，未实现 page table）

## 9. TaskRunner 的真实价值

综合 TADR-101 ~ TADR-106、tests 三件套（test_cuda_scheduler 8/8、test_gpu_architecture 11/11、test_gpu_phase2 12/12）：

> **TaskRunner 唯一真实价值是为 UsrLinuxEmu 提供"GPU 驱动契约的可测试 C++ 抽象 + 测试夹具 + 调试 CLI"。它是 UsrLinuxEmu 仓的"测试 + 文档 + 接口治理"补充，不是产品代码。**

具体价值：

1. **IGpuDriver 抽象（28 方法）+ 3 实现 DI 模式**：让 UsrLinuxEmu 的 `GPU_IOCTL_*` 契约可单元测试
2. **CudaStub mock 状态机**：验证 `next_queue_handle_` / `next_va_space_handle_` 单调 + existence tracking 语义
3. **CLI 调试工具**：`./build/taskrunner cuda_alloc/cuda_memcpy/cuda_launch/cuda_wait/cuda_va_space/cuda_queue` 6 个子命令
4. **端到端联调测试**：`test_cuda_scheduler` 跑通 `cudaMalloc → cudaMemcpy → cudaLaunchKernel → cudaWait` 全链路
5. **跨仓治理**：TADR-101~106 显式化 TaskRunner ↔ UsrLinuxEmu 接口契约，避免 ABI 漂移

## 10. 演化路径建议

### 10.1 立即可做（1 周内，建议作为 P0 修复）

1. **修复 `CudaScheduler` 抽象泄漏**（见 [tadr-109-igpu-driver-uniform-scheduling.md](../adr/tadr-109-igpu-driver-uniform-scheduling.md)）：
   - 删除 `dynamic_cast<CudaStub*>`（`cuda_scheduler.cpp:100, 146, 187, 226, 268`）
   - 改用 `IGpuDriver` 抽象的 `alloc_bo/free_bo`/`submit_*` 方法统一调度

### 10.2 短期（1-3 月）

2. **H-3.5 follow-up**：让 MockGpuDriver 也实现 guard，关闭 T6-T9 mock-behavior deviation
3. **H-7 ADR 跟踪**（[tadr-105-h7-deferred.md](../adr/tadr-105-h7-deferred.md)）：在 UsrLinuxEmu owner 端推动 3 个上游 issue 修复
4. **决策点**：是否启动 Vulkan Compute stub 实施？需 Phase 3 owner 触发后启动 `vk_compute_stub.cpp`（Layer 4）

### 10.3 中期（3-6 月，可选 PoC）

5. **最小 libcuda.so 表面 PoC**：`umd-evolution` 范畴，详见 [../umd-evolution/vision.md](../../umd-evolution/vision.md)
6. **Doorbell mmap 旁路 PoC**（AMD ROCm `amd_aql_queue.cpp:482-493` 模式）

### 10.4 长期（6-12 月）

7. **不建议**全面演化为 NVIDIA-equivalent UMD。聚焦于"UsrLinuxEmu 联调测试基础设施 + Vulkan Compute stub"两条线
8. **若一定要演化为 UMD**：先实施 CUmodule/CUfunction 加载（这是工作量最大的部分）

## 11. 关键缺失能力清单（按优先级）

| 优先级 | 能力 | 影响 | 是否在 test-fixture 范畴 |
|--------|------|------|--------------------------|
| **P0** | `CudaScheduler` 抽象泄漏（dynamic_cast 紧耦合） | IGpuDriver 抽象价值打折 | ✅ test-fixture |
| **P0** | MockGpuDriver guard 偏差（T6-T9） | 测试覆盖不完整 | ✅ test-fixture |
| **P1** | R2 mapping LOW32 溢出风险 | `next_queue_handle_` > UINT32_MAX 时 submit_batch 失败 | ✅ test-fixture |
| **P1** | Vulkan Compute stub 完全缺失 | `vk_compute_stub.cpp` 是 Layer 4 占位符 | ⏸️ Phase 3 deferred |
| **P2** | Stream object 模型 | `cudaStream_t` 不存在 | 📋 umd-evolution |
| **P2** | Context 模型 | `CUcontext` 不存在 | 📋 umd-evolution |
| **P3** | CUDA Module 加载 | CUBIN/PTX 解析完全缺失 | 📋 umd-evolution |
| **P3** | Unified Memory | `cudaMallocManaged` 未支持 | 📋 umd-evolution |
| **P3** | CUDA Graph | `cudaGraphCreate/Capture` 未支持 | 📋 umd-evolution |

## References

### TaskRunner 端关键 ADR（[docs/test-fixture/adr/](../adr/)）

- `tadr-101-stub-tracker.md` - Stub 定位（test-fixture 范畴规范）
- `tadr-102-igpu-driver.md` - IGpuDriver 抽象（28 方法）
- `tadr-103-h3-phase2.md` - H-3 Phase 2 消费方视角
- `tadr-104-r2-mapping.md` - R2 mapping 契约
- `tadr-105-h7-deferred.md` - H-7 deferred 跟踪
- `tadr-106-test-fixture-scope-clarification.md` - test-fixture 范畴规范
- `tadr-109-igpu-driver-uniform-scheduling.md` - **IGpuDriver 统一调度修复**（待办）

### 关键源代码

- `include/igpu_driver.hpp`（311 行）— 28 方法抽象
- `include/cuda_stub.hpp`（226 行）+ `src/cuda_stub.cpp`（506 行）— Stub 实现（注释 vs 实现矛盾点）
- `include/cuda_scheduler.hpp`（221 行）+ `src/cuda_scheduler.cpp`（369 行）— **抽象泄漏位置**（line 100, 146, 187, 226, 268）
- `include/gpu_driver_client.h`（576 行）— 唯一真实 ioctl（11 个 ioctl 调用）
- `src/cmd_cuda.cpp`（407 行）— CLI 6 个子命令

### 测试

- `tests/test_cuda_scheduler.cpp`（L99 验证 stub 模式 D2H 是全 0）
- `tests/test_gpu_phase2.cpp`（T6-T9 mock-behavior deviation 证据）
- `tests/mock_gpu_driver.hpp`（353 行，headless 测试夹具）

### 原始 TaskRunner 调度框架（GPU 无关部分）

- `include/TaskRunner.h`（458 行）
- `sample/main.cpp`（109 行，纯调度示例）

## Cross-References

Related research documents in this repository:

- [UsrLinuxEmu GPU 驱动设计意图](../shared/research/usrlinuxemu-gpu-driver-design-2026-06-24.md) `[SHARED SCOPE]`
- [NVIDIA CUDA 用户态驱动架构](../umd-evolution/research/external-nvidia-cuda-umd-2026-06-24.md) `[UMD-EVOLUTION SCOPE]`
- [AMD ROCm 用户态驱动架构](../umd-evolution/research/external-amd-rocm-umd-2026-06-24.md) `[UMD-EVOLUTION SCOPE]`

Upstream documents:

- [UsrLinuxEmu ADR-036 — 3-Way Architectural Separation](../../../../docs/00_adr/adr-036-three-way-separation.md)
- [UsrLinuxEmu ADR-001 — User-Mode Emulation](../../../../docs/00_adr/adr-001-user-mode-emulation.md)
- [Umbrella gap-analysis.md](../../umd-evolution/gap-analysis.md)