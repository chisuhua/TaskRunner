---
SCOPE: UMD-EVOLUTION
STATUS: ACCEPTED
DATE: 2026-06-24
RESEARCH_ID: bg_68947682
RESEARCH_TITLE: 调研 AMD ROCm 用户态驱动架构
RELATED_RESEARCH:
  - ../shared/research/usrlinuxemu-gpu-driver-design-2026-06-24.md
  - ../test-fixture/research/taskrunner-positioning-2026-06-24.md
  - ./external-nvidia-cuda-umd-2026-06-24.md
---

# AMD ROCm 用户态驱动架构调研

> **调研日期**：2026-06-24
> **目标**：理解 AMD ROCm 用户态驱动（UMD）的职责边界，重点关注"哪些硬件相关设置在用户态完成"
> **证据基线**：rocm-systems monorepo @ `9900764efe` (develop HEAD 2026), Linux kernel `kfd_ioctl.h` @ master

## 执行摘要

1. **ROCm 7.1+ 已统一到 rocm-systems monorepo**——HSA Runtime、ROCt thunk、HIP/COMGR/AQL Profile 都在一个仓库
2. **Doorbell MMIO 旁路是核心模式**：用户态直接 `*(signal_.hardware_doorbell_ptr) = uint64_t(value);` 写 WC 内存，内核**不参与**单个 packet 提交
3. **用户态承担 11 类硬件相关设置**：AQL packet 构造、Ring buffer 自管理、Doorbell 写入、CWSR 分配、Trap handler、Built-in blit shader、SDMA packet 构造、能力检测、Topology 解析、Code Object 解析、Kernel argument 序列化
4. **KFD 必须承担的工作**（`/dev/kfd` 通过 `AMDKFD_IOC_*` 暴露）：GPU 调度、页表管理、中断处理、GPU reset、VM 地址空间管理
5. **对 UsrLinuxEmu 的启示**：TaskRunner 的 `IGpuDriver` 抽象契合 ROCm 6.x driver interface 重构方向；Doorbell 旁路是 Phase D 的关键 PoC

## 1. ROCm 软件栈全景

### 1.1 仓库现状（2026 年）

ROCm 7.1+ 已统一到 **rocm-systems monorepo**。原分散仓库（ROCR-Runtime/HIP/CLR）已合并。

| 组件 | 库名 | 位置 | 角色 |
|------|------|------|------|
| HSA Runtime | `libhsa-runtime64.so` | `projects/rocr-runtime/runtime/hsa-runtime/` | hsa_* API |
| ROCt thunk | `libhsakmt.so` | `projects/rocr-runtime/libhsakmt/src/` | **薄 ioctl 包装到 /dev/kfd** |
| HIP/COMGR/OpenCL | `libamdhip64.so` | `projects/clr/hipamd/src/` | hip_* API + Code object 加载 |
| AQL Profile | `libhsa-amd-aqlprofile64.so` | `projects/aqlprofile/` | 性能采样 |
| HIPRTC | `libhiprtc.so` | `projects/clr/hipamd/src/hiprtc/` | 运行时 C++ 编译 |

### 1.2 关键架构决定

- **HSA Runtime** 显式声明自己是 **"a thin, user-mode API"**，不是驱动
- **libhsakmt** 是 **"a 'thunk' interface to the ROCm kernel driver (ROCk), used by the runtime"** —— to-KFD 的桥
- 6.4 起 AMD 把 "ROCm userspace components" 重命名为 "ROCm toolkit"，把内核驱动重命名为 "Instinct driver"（原 ROCk，2026 年迁到 `ROCm/instinct-driver`）

## 2. 用户态 vs 内核态职责划分

### 2.1 ⭐ 用户态完成的"硬件相关"设置（重点）

> 这是 UsrLinuxEmu 用户态驱动应该承担的工作。每一项在用户态做而不下内核，要么是为了低延迟、要么是为了避免 IPC 切换。

#### A. AQL Packet 构造与 Doorbell 写入（零内核路径）

**证据**：`amd_aql_queue.cpp:482-493`

```cpp
void AqlQueue::StoreRelaxed(hsa_signal_value_t value) {
  if (core::Runtime::runtime_singleton_->thunkLoader()->IsDTIF() ||
        core::Runtime::runtime_singleton_->thunkLoader()->IsDXG()) {
    HSAKMT_CALL(hsaKmtQueueRingDoorbell(queue_id_, value));
  } else {
    // Hardware doorbell supports AQL semantics.
    _mm_sfence();
    *(signal_.hardware_doorbell_ptr) = uint64_t(value);  // ← 关键!直接 MMIO write
    /* signal_ is allocated as uncached so we do not need read-back to flush WC */
  }
}
```

**设计意义**：这是 ROCm 的 **doorbell 旁路** 优化——一旦 KFD 分配了 doorbell offset，用户态直接写 WC 内存地址，内核**不参与**单个 packet 提交。整条 dispatch 链：用户态填写 AQL packet → 写 doorbell → CP 硬件自取 → 无内核介入。

#### B. AQL Ring Buffer 自管理

**证据**：`amd_aql_queue.cpp:589-637` `AqlQueue::AllocRegisteredRingBuffer`

用户态自行：
- 分配 ring buffer（`agent_->system_allocator()` 或 `coarsegrain_allocator()`）
- 大小约束：4 KiB 对齐，2 的幂 packets，使用 `core::AqlPacket` 模板
- 用 `HSA_PACKET_TYPE_INVALID` 填满整个 ring
- 决定是否用 device-memory ring（需 Large BAR）

#### C. EOP Buffer & CWSR（Context Save/Restore）分配

**证据**：`queues.c:588-679` `handle_concrete_asic` + `update_ctx_save_restore_size`

用户态（libhsakmt）计算并分配：
- **EOP buffer**（End-Of-Pipe）：4 KiB VRAM 缓冲
- **CWSR area** = `(ctx_save_restore_size + debug_memory_size) * NumXcc`，需 `madvise(MADV_DONTFORK)` 防 fork
- **VGPR/SGPR 尺寸按 GFX 版本查表**（`queues.c:128-164`）：`hsakmt_get_vgpr_size_per_cu` 根据 `GFX_VERSION_*` 选 0x40000/0x60000/0x80000

#### D. Trap Handler 加载

- HSA Runtime 自带 **trap handler V1/V2** 二进制数据（头文件 `amd_trap_handler_v1.h`, `amd_trap_handler_v2.h`）
- 用户态通过 `AMDKFD_IOC_SET_TRAP_HANDLER` ioctl 把 trap handler 地址（TBA/TMA）告知 KFD
- 让 KFD 在发生 wave trap 时把控制权交回用户态提供的 handler

#### E. Built-in Blit Shaders 编译

**证据**：`amd_blit_kernel.cpp:57` + `blit_shaders/`

```cpp
static constexpr const char kBlitKernelSource_[] = R"(
  shader CopyAligned
    type(CS)
    user_sgpr_count(2)
    sgpr_count(32)
    ...
  end
)";
```

- **用户态自带** 汇编源码（DSL → 通过 `create_blit_shader_header.sh` 编译为 `amd_blit_shaders.h`）
- 运行时根据 GFXIP 版本（7/8/9/10/11/12）选对应 shader
- 用于：host↔device copy（当无 SDMA 或被禁用时）

#### F. SDMA Packet 构造

**证据**：`amd_blit_sdma.cpp:75-114`

用户态构造 SDMA packet（`SDMA_PKT_COPY_LINEAR`, `SDMA_PKT_CONSTANT_FILL`, `SDMA_PKT_FENCE`, `SDMA_PKT_ATOMIC` 等）—— 完全理解 SDMA 微架构，按 GFX 版本（`SDMA_PKT_GCR_GFX1250` vs `SDMA_PKT_GCR_GFX12`）分模板特化。

#### G. 设备能力检测

**证据**：`amd_gpu_agent.cpp:101-221` `GpuAgent::GpuAgent`

- `GpuAgent` 接收从 KFD 读出的 `HsaNodeProperties`
- 用户态自行决定：
  - `max_wave_scratch_`：GFX12 → `MAX_WAVE_SCRATCH_GFX12 = 67106816`，否则 `MAX_WAVE_SCRATCH = 8387584`
  - `supported_isas_` 列表
  - `wavefront size`（wave32 vs wave64）
- 完全是用户态策略，不下内核

#### H. Topology 解析（来自 sysfs）

**证据**：`topology.c:56-66` + `topology.c:380-460`

```c
#define KFD_SYSFS_PATH "/sys/devices/virtual/kfd/kfd/topology"
#define KFD_SYSFS_PATH_NODES "%s/nodes"
// fopen(KFD_SYSFS_PATH_NODES + "/N/gpu_id", "r")  // 读 GPU ID
// fopen(KFD_SYSFS_PATH + "/system_properties", "r") // 读全局属性
```

- libhsakmt 直接读 sysfs，**不通过 ioctl** 拿 topology
- 仅 `gpu_id` 通过 `hsaKmtGetNodeProperties()` → KFD ioctl 拿

#### I. Code Object（.hsaco / fatbin）解析

**证据**：`hip_fatbin.cpp:44-100` + `hip_code_object.cpp:27-47`

HIP 用户态：
- 调 `comgr`（CodeObject Manager）解析 fat binary → per-device `.hsaco`
- 解析 ELF 头、symbol table、kernel metadata
- 缓存每个 device 对应的 program 对象
- 完全在用户态做，**不需要 KFD 参与**

#### J. Kernel Argument 序列化

HIP `hipLaunchKernel` → 用户态：
- 按 kernel signature 把 args 打包到 kernarg buffer（用户态分配的 GPU 可见内存）
- 构造 AQL `kernel_dispatch_packet_t`（64 字节定长，详见 [HSA Runtime API 7.2.3](https://rocm.docs.amd.com/projects/ROCR-Runtime/en/docs-7.2.3/api-reference/api.html)）
- 写 dispatch packet 到 ring buffer 槽位 → 写 doorbell

### 2.2 KFD 内核态必须做的事

**完整 ioctl 列表**（证据：[`kfd_ioctl.h`](https://github.com/torvalds/linux/blob/master/include/uapi/linux/kfd_ioctl.h), v1.23）：

| Ioctl 编号 | 功能 | 用户态典型 caller |
|-----------|------|------------------|
| `0x01` GET_VERSION | 版本协商 | `hsaKmtGetVersion` (`version.c:44`) |
| `0x02` CREATE_QUEUE | 分配 Compute/SDMA queue + 返回 doorbell_offset | `hsaKmtCreateQueueV2` (`queues.c:806`) |
| `0x03` DESTROY_QUEUE | 销毁 queue | `queues.c:934` |
| `0x05` GET_CLOCK_COUNTERS | GPU/CPU 同步时间戳 | `hsaKmtGetClockCounters` (`time.c:46`) |
| `0x06/0x14` GET_PROCESS_APERTURES[_NEW] | LDS/scratch/GPUVM 进程地址窗口 | `fmm.c:2334/2344` |
| `0x07` UPDATE_QUEUE | 改 queue 大小/优先级 | `queues.c:913` |
| `0x08-0x0C` EVENT 系列 | 信号与事件 | `events.c` |
| `0x0D-0x10` DBG 系列 | 调试 | `debug.c` |
| `0x11` SET_SCRATCH_BACKING_VA | 配置 scratch 物理页 | `fmm.c:1581` |
| `0x13` SET_TRAP_HANDLER | 设 TBA/TMA 寄存器 | `queues.c:1030` |
| `0x15` ACQUIRE_VM | 从 drm_fd 借 VM | `fmm.c:2434` |
| `0x16` ALLOC_MEMORY_OF_GPU | 分配 BO（VRAM/GTT/doorbell/MMIO） | `fmm.c:1237` |
| `0x17` FREE_MEMORY_OF_GPU | 释放 BO | `fmm.c:1273/1280` |
| `0x18/0x19` MAP/UNMAP_MEMORY_TO_GPU | 把 BO 映射到指定 GPU 的页表 | `fmm.c:3447/3474` |
| `0x1A` SET_CU_MASK | 启用/禁用 CU | `queues.c:962` |
| `0x1B` GET_QUEUE_WAVE_STATE | 取 wave 上下文（供调试） | `queues.c:991` |
| `0x1C-0x1E` DMABUF 系列 | 跨进程 BO 共享 | `fmm.c:4080/4126/4203` |
| `0x1F` SMI_EVENTS | 订阅 GPU reset / page fault / thermal 事件 | `events.c:519` |
| `0x20-0x23` IPC 系列 | 跨进程信号/内存共享 | `fmm.c:4257/4310` |
| `0x24+` SVM / CRIU / DEBUG_TRAP / RUNTIME_ENABLE | ROCm 5/6/7 扩展 | `svm.c`, `debug.c:363+` |

**KFD 内核态承担的硬性工作**：
- 真实 GPU 调度：queue 调度、preemption
- GPU 页表管理：`MAP_MEMORY_TO_GPU` 走 HMM/SVM
- 中断处理：GPU fault、completion signal、thermal
- doorbell offset 分配：创建 queue 时返回 `doorbell_offset`
- KFD process 生命周期：`AMDKFD_IOC_CREATE_PROCESS`（`openclose.c:347`）
- GPU reset 与 RAS

### 2.3 amdgpu DRM 做的

- BO（Buffer Object）分配/释放/映射（VRAM/GTT）
- 同步对象（`syncobj`）
- **不是 ROCm 计算的常规路径**；ROCm 计算的 BO 申请主要走 KFD `ALLOC_MEMORY_OF_GPU`，仅在需要 GDS/OA/dmabuf 等 DRM 资源时才用 `libdrm_amdgpu`
- libhsakmt 在 `fmm.c` 与 `memory.c` **同时**调用两套

## 3. 初始化流程（时序证据）

**`hsa_init()` → `Runtime::Acquire()` → `Runtime::Load()` → `KfdDriver::Init()`**

1. `hsa_init` 入口（[`runtime.cpp:116-141`](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/runtime.cpp#L116-L141)）
2. `Load()` 创建 `ThunkLoader`，dlsym `hsaKmtOpenKFD` 等函数指针（[`thunk_loader.cpp:114-121`](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/thunk_loader.cpp#L114-L121)）
3. `KfdDriver::Init()` 调 `hsaKmtRuntimeEnable` / `hsaKmtGetVersion` / `hsaKmtGetRuntimeCapabilities`（[`amd_kfd_driver.cpp:114-127`](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/rocr-runtime/runtime/hsa-runtime/core/driver/kfd/amd_kfd_driver.cpp#L114-L127)）
4. `DiscoverDrivers()` → `KfdDriver::DiscoverDriver` / `XdnaDriver` / `Virtio`（[`amd_topology.cpp:85-100`](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_topology.cpp#L85-L100)）
5. `DiscoverGpu()` 创建 `GpuAgent`（[`amd_topology.cpp:149-212`](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_topology.cpp#L149-L212)）
6. `hsaKmtOpenKFDCtx()` → `open("/dev/kfd", O_RDWR | O_CLOEXEC)`（[`openclose.c:211-217`](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/rocr-runtime/libhsakmt/src/openclose.c#L211-L217)）
7. `topology_sysfs_get_system_props` 读 sysfs
8. HIP 端：任何 HIP API → `HIP_INIT_API` → 首次调 `hsa_init` → 通过 `hsa_iterate_agents` 把 `g_devices` 填满（[`hip_device.cpp:447-460`](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/clr/hipamd/src/hip_device.cpp#L447-L460)）

## 4. 关键设计模式

### 4.1 Doorbell MMIO 旁路（Zero-Copy Dispatch）

**核心**：一次 ioctl（`AMDKFD_IOC_CREATE_QUEUE`）拿 doorbell offset 后，**所有 packet 提交都不再走 ioctl**。用户态直接写 WC 内存。

→ **UsrLinuxEmu 启示**：`GpuDriverClient` 应该在 `GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH` 之外，允许用户态直接写 doorbell（doorbell 页应通过 mmap 暴露给用户）。

### 4.2 Ring Buffer 自管理

用户态分配和回收 ring buffer，KFD 只在 create/destroy 时介入。

→ **UsrLinuxEmu 启示**：`IGpuDriver::CreateQueue` 应当返回 ring 内存 + doorbell offset，由 `CudaScheduler` 自行填 AQL/HIP packet。

### 4.3 libhsakmt 是薄包装

绝大多数 `hsaKmt*` 函数就只做**一个 `ioctl(2)` 调用**，逻辑都集中在 HSA Runtime 端。

→ **UsrLinuxEmu 启示**：`IGpuDriver` 接口应保持薄（ioctl 风格），业务逻辑放在 `CudaScheduler` / `CudaStub` 层。TaskRunner 现有设计**已对齐此模式**。

### 4.4 Thunk 解耦

HSA Runtime 通过 `ThunkLoader` 在运行时 dlsym 加载 libhsakmt，而 libhsakmt 自身**也是独立 .so**。

→ **UsrLinuxEmu 启示**：`GpuDriverClient` 已经走 dlopen `plugin_gpu_driver.so`，符合这个解耦模式。

### 4.5 设备驱动抽象化（2024 新增）

证据：commit [`69ba32f`](https://github.com/ROCm/rocm-systems/commit/69ba32fa95010674d22ea8826f543519bc549201) (2024-06-25)

> "Add a new driver interface as a core ROCr component. The driver component provides an interface for ROCr to interact with agent kernel-model drivers in a generic way. This interface will be used to interact with the XDNA NPU driver."

→ **UsrLinuxEmu 启示**：TaskRunner 的 `IGpuDriver` 抽象（2026-06-19 S5 已实现）正契合这条主线。

## 5. 对 UsrLinuxEmu 用户态驱动的设计启示

### 5.1 接口分层（类比）

| ROCm 组件 | UsrLinuxEmu 对应 |
|----------|------------------|
| `libhsakmt.so`（薄 ioctl 包装）| `GpuDriverClient` |
| `libhsa-runtime64.so`（业务逻辑）| `CudaScheduler` |
| `libamdhip64.so`（HIP API）| `CudaStub`（mock 层）/ `umd-evolution` 范畴的真 libcuda |
| `AMDKFD_IOC_*` 30+ 命令 | `GPU_IOCTL_*`（UsrLinuxEmu 当前 11 个命令）|

### 5.2 哪些工作应该下沉到 GpuDriverClient（用户态）

✅ **强烈建议放用户态**（基于 ROCm 模式）：

1. **AQL/HIP packet 构造**（类比 `amd_aql_queue.cpp:488-491`）
2. **Ring buffer 内存分配**（类比 `AqlQueue::AllocRegisteredRingBuffer`）
3. **Doorbell 直接写入**（WC mmap 暴露给用户态）
4. **CWSR 区域分配**（GFX 版本查表：`hsakmt_get_vgpr_size_per_cu`）
5. **Built-in trap handler 加载**（类似 `amd_trap_handler_v1/v2.h`）
6. **Blit shader 自带 + 运行时编译**（类似 `blit_shaders/`）
7. **SDMA packet 模板特化**（类似 `BlitSdma<useGCR, scopeFields>`）
8. **Topology 解析**（sysfs 风格，或通过 ioctl 拿 properties）
9. **Code Object 解析**（PE/ELF）
10. **Kernel argument 序列化**

### 5.3 哪些是 UsrLinuxEmu host plugin 的硬性工作

🛡️ **必须由 plugin（内核/host 模拟层）负责**：

1. **Doorbell offset 分配**（模拟 KFD 的 `CREATE_QUEUE` 行为）
2. **VM 地址空间管理**（类似 `MAP_MEMORY_TO_GPU` / `ACQUIRE_VM`）
3. **GPU page table 维护**（硬件不可避免）
4. **真正的"执行"模拟**（因为是用户态模拟，所有"硬件"行为都是 plugin 模拟）
5. **Event/Signal 等待与唤醒**（类似 `WAIT_EVENTS` + KFD `epoll`）
6. **BO/GEM 分配**（GPU 内存模拟）

### 5.4 性能与兼容性建议

- **Doorbell MMIO**：确保 `GPU_IOCTL_CREATE_QUEUE` 之后，plugin 通过 mmap 把 doorbell 页暴露给用户态，避免每次 dispatch 走 ioctl
- **保留 batch 接口**：`GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH` (0x01) 适合 fallback 路径，但 doorbell 旁路才是高性能路径
- **Trap handler 注入**：`GPU_IOCTL_REGISTER_FIRMWARE_CB` (0x03) 已存在，这是把 host callback 暴露给 GPU 模拟代码的接口——与 ROCm 的 trap handler 模式一致
- **VA space 抽象**：`GPU_IOCTL_CREATE_VA_SPACE` (0x30) — 与 KFD `ACQUIRE_VM` 同构，TaskRunner H-3（Phase 2）已实现，验证了这个设计
- **Fence ID 机制**：S3.5 fence_id 透传（2026-05-13）类似 ROCm 的 `KFD_IOC_WAIT_EVENTS` 返回 event_id

## 6. 未确认/局限性

- **libdrm_amdgpu 路径细节**：未深入阅读 amdgpu_drm.h 与 libdrm_amdgpu 的具体 ioctl（drm 端，`/dev/dri/renderD128`）。如 UsrLinuxEmu 未来要模拟这个接口可再调研
- **amdgpu 驱动的 MES / RLC 固件接口**（GFX11+ 的 MicroEngine Scheduler）未涉及。ROCm 6.x+ 正在从 RLC 调度迁到 MES（用户态驱动 + 内核配合），但 TaskRunner 当前不涉及
- **XNMI**（用户态 memory-mapped GPU interrupts）在 ROCm 6.2 引入（`KFD_IOC_SMI_EVENTS` 演进），未在本次深入
- **PAL (Platform Abstraction Library)** 后端（`ROCCLR_ENABLE_PAL=ON`，用于 Windows）：Windows 路径走完全不同的用户态栈，不在 Linux 调研范围

## References

### ROCm 核心源码（rocm-systems monorepo @ `9900764`）

- [HSA Runtime 主入口](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/runtime.cpp#L116-L141)
- [HSA Runtime::Load](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/runtime.cpp#L2479-L2554)
- [Topology discovery](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_topology.cpp#L85-L212)
- [KfdDriver::Init](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/rocr-runtime/runtime/hsa-runtime/core/driver/kfd/amd_kfd_driver.cpp#L114-L130)
- [AQL Queue 构造](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_aql_queue.cpp#L81-L200)
- [Ring buffer 自管理](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_aql_queue.cpp#L589-L662)
- [⭐ Doorbell 直接 MMIO 写入](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_aql_queue.cpp#L482-L493)
- [Built-in blit shader 源](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_blit_kernel.cpp#L57-L100)
- [SDMA packet 模板特化](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_blit_sdma.cpp#L75-L114)
- [GpuAgent 能力检测](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_gpu_agent.cpp#L101-L221)

### libhsakmt（KFD 桥）

- [打开 /dev/kfd](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/rocr-runtime/libhsakmt/src/openclose.c#L171-L224)
- [Topology sysfs 路径常量](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/rocr-runtime/libhsakmt/src/topology.c#L56-L66)
- [CREATE_QUEUE ioctl 调用](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/rocr-runtime/libhsakmt/src/queues.c#L762-L806)
- [CWSR/EOP 分配](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/rocr-runtime/libhsakmt/src/queues.c#L588-L679)
- [VGPR/SGPR GFX 版本表](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/rocr-runtime/libhsakmt/src/queues.c#L128-L164)

### HIP

- [hipModuleLoad 入口](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/clr/hipamd/src/hip_module.cpp#L41-L62)
- [DynCO::loadCodeObject](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/clr/hipamd/src/hip_code_object.cpp#L27-L47)
- [FatBinaryInfo 解析](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/clr/hipamd/src/hip_fatbin.cpp#L44-L94)
- [hipInit/hipDeviceGet](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/clr/hipamd/src/hip_context.cpp#L145-L170)
- [ihipDeviceGet/Count](https://github.com/ROCm/rocm-systems/blob/9900764efe4278549fd5ff9fd43c95786376733d/projects/clr/hipamd/src/hip_device.cpp#L380-L460)

### Linux Kernel KFD ioctl

- [`kfd_ioctl.h` v1.23 完整定义](https://github.com/torvalds/linux/blob/master/include/uapi/linux/kfd_ioctl.h)

### 架构演进证据

- [2024-06 Driver 抽象 commit](https://github.com/ROCm/rocm-systems/commit/69ba32fa95010674d22ea8826f543519bc549201)
- [2025-04 Instinct driver 重命名博客](https://rocm.blogs.amd.com/ecosystems-and-partners/instinct-gpu-driver/README.html)
- [ROCR 6.2.1 官方文档](https://rocm.docs.amd.com/projects/ROCR-Runtime/en/docs-6.2.1/what-is-rocr-runtime.html)
- [AQL API 7.2.3 文档](https://rocm.docs.amd.com/projects/ROCR-Runtime/en/docs-7.2.3/api-reference/api.html)

### UsrLinuxEmu 现有接口（对比参照）

- `plugins/gpu_driver/shared/gpu_ioctl.h`（UsrLinuxEmu 端）— `GPU_IOCTL_*` 接口（已与 ROCm `AMDKFD_IOC_*` 设计模式一致）

## Cross-References

Related research documents in this repository:

- [TaskRunner 自身定位与能力边界](../test-fixture/research/taskrunner-positioning-2026-06-24.md) `[TEST-FIXTURE SCOPE]`
- [UsrLinuxEmu GPU 驱动设计意图](../shared/research/usrlinuxemu-gpu-driver-design-2026-06-24.md) `[SHARED SCOPE]`
- [NVIDIA CUDA 用户态驱动架构](./external-nvidia-cuda-umd-2026-06-24.md) `[UMD-EVOLUTION SCOPE]`

Upstream documents:

- [Umbrella gap-analysis.md](../gap-analysis.md)
- [Umbrella vision.md](../vision.md)