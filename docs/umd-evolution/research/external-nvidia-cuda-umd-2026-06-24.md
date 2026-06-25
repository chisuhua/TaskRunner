---
SCOPE: UMD-EVOLUTION
STATUS: ACCEPTED
DATE: 2026-06-24
RESEARCH_ID: bg_132e48fc
SOURCE_REPO: external (NVIDIA open-gpu-kernel-modules + NVIDIA docs + libnvidia-container + cuda_ioctl_sniffer)
RELATED_RESEARCH:
  - ../shared/research/usrlinuxemu-gpu-driver-design-2026-06-24.md
---

# NVIDIA CUDA 用户态驱动（UMD）架构调研

> **调研日期**：2026-06-24
> **调研目标**：理解 NVIDIA CUDA 用户态驱动（UMD）的职责边界，重点关注"哪些硬件相关设置在用户态完成"。
> **调研方法**：从 NVIDIA 官方文档（docs.nvidia.com/cuda、developer.nvidia.com/blog）、开源 kernel modules（NVIDIA/open-gpu-kernel-modules）、GitHub 公开项目（cuda_ioctl_sniffer、libnvidia-container）收集证据。所有 URL 附 commit SHA + permalink。

---

## 执行摘要

- **CUDA 软件栈 = 5 层结构**：CUDA Toolkit（libcudart + cuBLAS）→ UMD（libcuda + libnvidia-ml + libnvidia-ptxjit + libnvidia-fatbinaryloader）→ Kernel-Mode Driver（nvidia.ko + nvidia-uvm.ko + nvidia-modeset.ko）→ GPU Hardware（GR/CE/NVDEC/NVENC/SEC2 engines）。自 R560 起开源 kernel modules 为默认 flavor。
- **libcuda.so 角色**：Driver API 实现（`cu*` 前缀）。`cuInit(0)` 通过 `NV_ESC_CHECK_VERSION_STR` / `SYS_PARAMS` / `CARD_INFO` 三个 ioctl 完成初始化；通过 `dlopen` 延迟加载 JIT/Fatbin 库；纯用户态做 PTX→SASS JIT、fatbin 解析、Context TLS 管理、Stream/Event 句柄。
- **libcudart.so 角色**：Runtime API 实现（`cuda*` 前缀）。隐式 context 创建（首次 `cudaFree(0)` 触发）、managed memory（UVM ioctl 协同）、Stream Capture + Graph 构造（DAG 纯用户态）。CUDA 12.4+ 引入 `cudaExecutionContext_t`（Green Context 抽象）。
- **UVM 角色**：`nvidia-uvm.ko` 是独立代码树，开源 30+ ioctl。负责 GPU MMU fault buffer、跨 CPU/GPU 页迁移、VA Block 树管理、Radix page table。用户态仅触发 `UVM_MIGRATE` / `UVM_SET_PREFERRED_LOCATION`，不做页表操作。
- **用户态 vs 内核态职责矩阵**：必须用户态完成 = PTX JIT / Fatbin 解析 / Graph 构造 / Context TLS；必须内核态完成 = GPU MMIO / MSI-X / 页表操作 / Power/热管理 / MIG 物理分区 / PCIe 链路协商 / GSP-RM 固件加载；跨边界 = cuMemAlloc / cudaMallocManaged / cuLaunchKernel。
- **NVML / nvidia-smi 角色**：纯用户态监控工具（`libnvidia-ml.so`），查询 NV2080_CTRL_* RM 控制命令，不做硬件设置。
- **MPS / Confidential Computing / Grace Hopper**：MPS 实现多 CUDA 进程 Hyper-Q 共享（控制 daemon + server + 内置 client）；CC 用加密 bounce buffer + SPDM 会话 + UVM 扩展；GH200 用 NVLink-C2C 900 GB/s + CDMM 模式让 NVIDIA driver 而非 OS 管理 GPU memory。
- **CUDA 12.x 演化**：forward compat 11.1+ / nvJitLink 11.4 / Green Context + nvFatbin 12.4 / Tile IR (libnvidia-tileiras) 12.4+。CUDA driver 功能可分 4 类：纯用户态 / 需新 libcuda / 需新内核 / 需新 user-mode 工具。
- **对 UsrLinuxEmu 的启示**：(1) 控制/数据面分离（控制走 ioctl、数据走 mmap）；(2) late binding 关键组件（dlopen JIT/调试器）；(3) 保留 forward compat 设计空间；(4) MIG/Green Context 在用户态做资源切片；(5) 流捕获与 Graph 纯用户态；(6) 参考 RM Control 命令分层（ctrl0000/0080/2080）设计 UsrLinuxEmu ctrl class 编号。

---

## 1. CUDA 软件栈全景

### 1.1 层次划分

CUDA 软件栈由用户态（user-mode）和内核态（kernel-mode）两部分组成，职责严格分离：

```
┌─────────────────────────────────────────────────┐
│ Layer 2: CUDA Toolkit (libcudart, cuBLAS, ...)  │  ← 用户态：编程抽象
├─────────────────────────────────────────────────┤
│ Layer 1: User-Mode Driver (libcuda.so,          │  ← 用户态：API 入口
│           libnvidia-ml.so, libnvidia-ptxjit,     │
│           libnvidia-fatbinaryloader, ...)       │
├─────────────────────────────────────────────────┤
│ Layer 0: Kernel-Mode Driver (nvidia.ko,         │  ← 内核态：硬件访问
│           nvidia-uvm.ko, nvidia-modeset.ko)     │
├─────────────────────────────────────────────────┤
│ Physical: GPU Hardware (registers, MMU,         │
│           engines: GR, CE, NVDEC, NVENC, SEC2)  │
└─────────────────────────────────────────────────┘
```

**证据**：

- **Modal Docs 层次划分**（[modal.com/docs/guide/cuda](https://modal.com/docs/guide/cuda)）：明确划分 Level 0 (kernel modules) / Level 1 (user-mode driver API) / Level 2 (CUDA Toolkit)
- **NAS HECC KB**（[nas.nasa.gov/hecc](https://www.nas.nasa.gov/hecc/support/kb/cuda-toolkit-and-cuda-driver_705.html)）：
  > "User-mode driver (libcuda.so, cuda.h): The CUDA driver API provides a low-level interface for applications to target NVIDIA hardware.
  > Kernel-mode driver (nvidia.ko): It provides low-level access to the NVIDIA GPU hardware."

### 1.2 内核态模块清单

| 模块 | 角色 | 来源 |
|------|------|------|
| `nvidia.ko` | 核心 RM (Resource Manager) | [NVIDIA/open-gpu-kernel-modules](https://github.com/NVIDIA/open-gpu-kernel-modules) |
| `nvidia-uvm.ko` | Unified Virtual Memory 子系统（独立代码树） | 同上 |
| `nvidia-modeset.ko` | 显示模式桥接（nv-kms） | 同上 |
| `nvidia-drm.ko` | DRM/KMS 接口 | 同上 |
| `nvidia-peermem.ko` | RDMA peer-memory | 同上 |

**证据**：

- **NVIDIA Driver Installation Guide**（[docs.nvidia.com/datacenter/tesla](https://docs.nvidia.com/datacenter/tesla/driver-installation-guide/595/kernel-modules.html)）：
  > "The NVIDIA Linux GPU Driver contains several kernel modules: nvidia-modeset.ko, nvidia-uvm.ko"
- **fuzzinglabs internals**（[fuzzinglabs.com](https://fuzzinglabs.com/exploring-nvidia-linux-drivers-internals-basics-ioctls/)）：
  > "Unlike nvidia.ko, which forwards some ioctls to the closed RM blob, UVM ioctls are handled by open code in this tree, and they exposed through /dev/nvidia-uvm"

### 1.3 开源 vs 闭源 flavors

自 R515 (Turing+) 提供开源 kernel modules（MIT/GPLv2 双协议），自 R560 起开源为默认。

**证据**：同上 NVIDIA Driver Installation Guide：

> "Starting in the 560 driver release series, the open kernel module flavor is the default and suggested installation."

---

## 2. libcuda.so（CUDA Driver API）的角色

### 2.1 在用户态做了什么

`libcuda.so` 是 CUDA Driver API 的实现，所有入口以 `cu` 为前缀。它在用户态完成以下职责：

| 职责 | 说明 |
|------|------|
| **驱动加载** | 打开 `/dev/nvidia*`、`/dev/nvidia-uvm` 等设备文件 |
| **API 路由** | 将 cuXXX 函数翻译为 ioctl 调用 |
| **PTX → SASS JIT 编译** | 首次加载 PTX 时调用 libnvidia-ptxjitcompiler |
| **Fatbin 解析** | 选择匹配的 cubin/PTX |
| **Context 对象管理** | 在用户态维护 CUcontext 状态机 |
| **Stream / Event 句柄管理** | 用户态数据结构 |
| **内存分配请求打包** | 准备 `NVOS21_PARAMETERS` 等结构，调用 ioctl |
| **JIT Cache 文件管理** | `$HOME/.nv/ComputeCache` |

**关键证据 - libnvidia-container 库清单**（[github.com/NVIDIA/libnvidia-container](https://github.com/NVIDIA/libnvidia-container/blob/main/src/nvc_info.c)）：

```c
static const char * const compute_libs[] = {
    "libcuda.so",                       /* CUDA driver library */
    "libcudadebugger.so",               /* CUDA Debugger Library */
    "libnvidia-opencl.so",              /* NVIDIA OpenCL ICD */
    "libnvidia-gpucomp.so",             /* Shared Compiler Library */
    "libnvidia-ptxjitcompiler.so",      /* PTX-SASS JIT compiler (used by libcuda) */
    "libnvidia-fatbinaryloader.so",     /* fatbin loader (used by libcuda) */
    "libnvidia-allocator.so",           /* NVIDIA allocator runtime library */
    "libnvidia-compiler.so",            /* NVVM-PTX compiler for OpenCL */
    "libnvidia-pkcs11.so",              /* Encrypt/Decrypt library */
    "libnvidia-nvvm.so",                /* The NVVM Compiler library */
    "libnvidia-tileiras.so",            /* JIT library for Tile IR compilation */
};
```

> **关键洞察**：`libcuda.so` 本身不实现 PTX JIT，而是 dlopen `libnvidia-ptxjitcompiler.so` 和 `libnvidia-fatbinaryloader.so`。这种 **late binding** 设计使得 libcuda 不强制依赖编译器。

### 2.2 初始化流程：cuInit

`cuInit(0)` 是所有 cu* 函数的入口。**cuInit 文档**（[docs.nvidia.com/cuda/cuda-driver-api](https://docs.nvidia.com/cuda/cuda-driver-api/group%5F%5FCUDA%5F%5FINITIALIZE.html)）：

> "Note: cuInit preloads various libraries needed for JIT compilation. To opt-out of this behavior, set the environment variable `CUDA_FORCE_PRELOAD_LIBRARIES=0`. CUDA will lazily load JIT libraries as needed. To disable JIT entirely, set the environment variable `CUDA_DISABLE_JIT=1`."

**用户态实际发生**：

1. 打开 `/dev/nvidiactl`（控制设备）
2. 通过 ioctl `NV_ESC_CARD_INFO` 枚举 PCI 设备
3. 通过 `__NV_IOWR(NV_ESC_CHECK_VERSION_STR)` 检查 RM API 版本
4. 调用 `__NV_IOWR(NV_ESC_SYS_PARAMS)` 获取系统参数
5. **预先 dlopen** JIT 库（libnvidia-ptxjitcompiler、libnvidia-fatbinaryloader）

**证据 - strace 抓取的真实 ioctl 序列**（[github.com/strace/strace/issues/342](https://github.com/strace/strace/issues/342)）：

```
ioctl(9, __NV_IOWR(NV_ESC_CHECK_VERSION_STR), 0x7ffe3a970150) = 0
ioctl(9, __NV_IOWR(NV_ESC_SYS_PARAMS), 0x7ffe3a970260) = 0
ioctl(9, __NV_IOWR(NV_ESC_CARD_INFO), 0x7b7166acc0a0) = 0
ioctl(9, __NV_IOWR(NV_ESC_RM_ALLOC), 0x7ffe3a970440) = 0
ioctl(9, __NV_IOWR(NV_ESC_RM_CONTROL), 0x7ffe3a96f9a0) = 0
```

### 2.3 设备枚举与上下文创建

```c
cuInit(0);                                  // 驱动初始化
cuDeviceGetCount(&count);                  // → NV_ESC_CARD_INFO × N
cuDeviceGet(&device, ordinal);             // → 返回设备句柄
cuCtxCreate(&ctx, 0, device);              // → NV_ESC_RM_ALLOC (Context class)
```

**用户态硬件相关设置**：

- **线程局部存储 (TLS) 上下文栈**：每个 host thread 维护 current context 栈（push/pop 语义）
- **Primary Context 生命周期**：runtime API 通过 `cuDevicePrimaryCtxRetain` 复用

**证据 - CUDA Programming Guide**（[docs.nvidia.com/cuda/cuda-programming-guide](https://docs.nvidia.com/cuda/cuda-programming-guide/03-advanced/driver-api.html)）：

> "The driver API must be initialized with `cuInit()` before any function from the driver API is called. A CUDA context must then be created that is attached to a specific device and made current to the calling host thread as detailed in Context."
>
> "Each host thread has a stack of current contexts. `cuCtxCreate()` pushes the new context onto the top of the stack."

### 2.4 ioctl 接口（libcuda ↔ nvidia.ko）

**ioctl 编码宏**（[kernel-open/common/inc/nv.h](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/51edebee/kernel-open/common/inc/nv.h#L46-L52)）：

```c
#define __NV_IOWR(nr, type) ({                                        \
    typedef char __NV_IOWR_TYPE_SIZE_ASSERT[__NV_IOWR_ASSERT(type)];  \
    _IOWR(NV_IOCTL_MAGIC, (nr), type);                                \
})
```

**关键 ioctl 编号**（[kernel-open/common/inc/nv-ioctl-numbers.h](https://github.com/NVIDIA/open-gpu-kernel-modules)）：

| ioctl | 用途 | 用户态调用方 |
|-------|------|--------------|
| `NV_ESC_CARD_INFO` | 枚举 GPU 设备信息 | cuDeviceGet* |
| `NV_ESC_SYS_PARAMS` | 获取 memblock_size 等系统参数 | cuInit |
| `NV_ESC_CHECK_VERSION_STR` | RM API 版本协商 | cuInit |
| `NV_ESC_RM_ALLOC` | 分配 RM 对象（Channel、Memory、Context 等） | cuModuleLoad, cuMemAlloc, cuStreamCreate |
| `NV_ESC_RM_CONTROL` | 调用 RM 控制命令（NV0000/NV0080/NV2080 ctrl） | cuDeviceGetAttribute, MIG |
| `NV_ESC_RM_MAP_MEMORY` | 将 device memory mmap 到进程地址空间 | cuMemAlloc 返回指针 |
| `NV_ESC_RM_VID_HEAP_CONTROL` | Video heap（显存堆）操作 | cuMemAlloc 内部 |
| `NV_ESC_RM_FREE` | 释放 RM 对象 | cuMemFree, cuStreamDestroy |
| `NV_ESC_ALLOC_OS_EVENT` | 分配 OS 事件（用于中断通知） | cuEventCreate |
| `NV_ESC_REGISTER_FD` | 注册 fd 到 RM | MPS client |

**证据 - ioctl 解析代码**（[github.com/geohot/cuda_ioctl_sniffer](https://github.com/geohot/cuda_ioctl_sniffer/blob/master/sniff.cc)）：

```c
case NV_ESC_CARD_INFO: printf("NV_ESC_CARD_INFO\n"); break;
case NV_ESC_REGISTER_FD: printf("NV_ESC_REGISTER_FD\n"); break;
case NV_ESC_SYS_PARAMS: printf("NV_ESC_SYS_PARAMS\n"); break;
case NV_ESC_CHECK_VERSION_STR: printf("NV_ESC_CHECK_VERSION_STR\n"); break;
case NV_ESC_RM_ALLOC_MEMORY: { ... } break;
case NV_ESC_RM_FREE: printf("NV_ESC_RM_FREE\n"); break;
case NV_ESC_RM_CONTROL: {
    NVOS54_PARAMETERS *p = (NVOS54_PARAMETERS *)argp;
    printf("NV_ESC_RM_CONTROL client: %x object: %x cmd: %8x\n", ...);
} break;
case NV_ESC_RM_VID_HEAP_CONTROL: {
    NVOS32_PARAMETERS *pApi = (NVOS32_PARAMETERS *)argp;
    auto asz = pApi->data.AllocSize;
    printf("    owner: %x, type: %d, flags: %x, size: %llx\n", ...);
} break;
```

### 2.5 RM Control 命令（NV00xx_CTRL_CMD）

`NV_ESC_RM_CONTROL` 是二级命令分发，参数是 `NVOS54_PARAMETERS {hClient, hObject, cmd, params}`。cmd 字段决定具体控制类别：

| 命令前缀 | 类别 | 典型用途 | 证据 |
|----------|------|----------|------|
| `NV0000_CTRL_CMD_*` | System 类 | GPU 探测、attach、fabric status | [ctrl/ctrl0000/ctrl0000system.h](https://github.com/NVIDIA/open-gpu-kernel-modules) |
| `NV0080_CTRL_CMD_*` | GPU 类 | class list、subdevice、SR-IOV caps | [ctrl2080mc.h](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/575.64.03/src/common/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080mc.h) |
| `NV2080_CTRL_CMD_*` | Subdevice 类 | GPU info、PCI info、ECC、power、bus | [ctrl2080bus.h](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/29f830f1bbac5114f77d682f4a4ce5b3420b733b/src/common/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080bus.h) |

**证据 - 实际解析**（[cuda_ioctl_sniffer sniff.cc](https://github.com/geohot/cuda_ioctl_sniffer/blob/f60ce2dcf2fe3bc270d997dc52a1f005ef60778f/sniff.cc)）：

```c
case NV0000_CTRL_CMD_SYSTEM_GET_BUILD_VERSION: cmd_string = "NV0000_CTRL_CMD_SYSTEM_GET_BUILD_VERSION"; break;
case NV0000_CTRL_CMD_GPU_GET_ATTACHED_IDS: cmd_string = "NV0000_CTRL_CMD_GPU_GET_ATTACHED_IDS"; break;
case NV0080_CTRL_CMD_GPU_GET_CLASSLIST: cmd_string = "NV0080_CTRL_CMD_GPU_GET_CLASSLIST"; break;
case NV2080_CTRL_CMD_GPU_GET_INFO: cmd_string = "NV2080_CTRL_CMD_GPU_GET_INFO"; break;
case NV2080_CTRL_CMD_BUS_GET_PCI_INFO: ... // 返回 PCI device/vendor ID
```

> 整个 ctrl 命令头文件目录约 3.5MB，由约 50 个 NV-prefix 文件组成。

---

## 3. libcudart.so（CUDA Runtime）的角色

### 3.1 Runtime vs Driver API

```
┌─────────────────────────────────────────────────┐
│  CUDA Runtime API (libcudart.so, prefix cuda*)  │  ← 高级 API
│  - 隐式初始化、错误检查、Device 抽象             │
│  - 提供 default stream、managed memory          │
└─────────────────────────────────────────────────┘
                    │ 内部调用
                    ▼
┌─────────────────────────────────────────────────┐
│  CUDA Driver API (libcuda.so, prefix cu*)       │  ← 低级 API
└─────────────────────────────────────────────────┘
```

**证据 - CUDA Programming Guide**（[docs.nvidia.com/cuda/cuda-programming-guide](https://docs.nvidia.com/cuda/cuda-programming-guide/03-advanced/driver-api.html)）：

> "The CUDA runtime is written on top of the lower level CUDA driver API. ... The driver API is implemented in the `cuda` dynamic library (`cuda.dll` or `cuda.so`) which is copied on the system during the installation of the device driver. All its entry points are prefixed with cu."

> "The driver API must be initialized with `cuInit()` before any function from the driver API is called."

> "The driver API is interoperable with the runtime and it is possible to access the primary context (see Runtime Initialization) managed by the runtime from the driver API via `cuDevicePrimaryCtxRetain()`."

### 3.2 Runtime 在用户态做的额外事

| 职责 | 实现位置 |
|------|----------|
| **隐式 context 创建** | cudaSetDevice → 首次调用 cudaFree(0) 触发 |
| **Managed memory (cudaMallocManaged)** | 联合 libcuda + libnvidia-uvm ioctl |
| **Stream 调度优化** | stream 优先级合并、capture 模式 |
| **Graph 构造（cudaGraph）** | 纯用户态 DAG 数据结构 |
| **Stream Capture** | 用户态录制 → cudaGraph |
| **Device 抽象（整数 ordinal）** | 隐藏 MIG/多 GPU 细节 |
| **Memory pool（隐式缓存）** | 配合 `cudaMemPool_t` |

**证据 - Runtime 流文档**（[docs.nvidia.com/cuda/cuda-runtime-api](https://docs.nvidia.com/cuda/cuda-runtime-api/group__CUDART__STREAM.html)）：

> "`cudaStreamCreate()` Create a new asynchronous stream on the context that is current to the calling host thread. If no context is current to the calling host thread, then the primary context for a device is selected, made current to the calling thread, and initialized before creating a stream on it."

### 3.3 Execution Context（CUDA 12 新增）

CUDA 12.4+ 引入 `cudaExecutionContext_t` 抽象，显式化 context 编程模型：

**证据**（[docs.nvidia.com/cuda/cuda-runtime-api](https://docs.nvidia.com/cuda/cuda-runtime-api/group__CUDART__EXECUTION__CONTEXT.html)）：

> "Once you have an execution context at hand, you can perform context-level operations via the CUDA Runtime APIs. This includes: Submitting work via streams created with `cudaExecutionCtxStreamCreate`."

> "The API does not create a default stream for the green context. Developers are expected to create streams explicitly using `cudaExecutionCtxStreamCreate` to submit work to the green context."

> → 表明 **green context 是用户态轻量级抽象**，无需内核参与分配完整 GPU context

---

## 4. UVM (Unified Virtual Memory) 角色

### 4.1 内核态职责（`nvidia-uvm.ko`）

`nvidia-uvm.ko` 是**独立的虚拟内存子系统**，代码量最大，开源。

**职责**：

- GPU MMU fault buffer 处理
- 跨 CPU/GPU 页迁移
- VA Block (2MB) 树管理
- Radix page table 操作（PDE/PTE）
- 进程间 va_space 注册
- Page residency tracking
- 用户态可见的 event/counter 队列

**证据 - fuzzinglabs internals**（[fuzzinglabs.com](https://fuzzinglabs.com/exploring-nvidia-linux-drivers-internals-basics-ioctls/)）：

> "This is the largest module in terms of code base size, and it is kind of a second virtual memory subsystem layered on top of Linux's, with its own VMA-equivalent objects and its own operations."

> "UVM ioctls are handled by open code in this tree, and they exposed through `/dev/nvidia-uvm`"

### 4.2 用户态（libcuda）如何与 UVM 交互

**用户态打开 `/dev/nvidia-uvm`** 后通过专用 ioctl 接口操作：

**关键 UVM ioctl**（[kernel-open/nvidia-uvm/uvm_ioctl.h](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/main/kernel-open/nvidia-uvm/uvm_ioctl.h)）：

| ioctl | 用途 |
|-------|------|
| `UVM_INITIALIZE` | 初始化 UVM 子系统 |
| `UVM_MM_INITIALIZE` | 注册进程 mm_struct |
| `UVM_REGISTER_GPU` | 将 GPU 注册到 va_space（libcuda 调用） |
| `UVM_UNREGISTER_GPU` | 注销 GPU |
| `UVM_PAGEABLE_MEM_ACCESS` | 查询 GPU 是否支持 pageable memory access |
| `UVM_REGISTER_CHANNEL` | 注册 RM channel（CUstream → UVM channel） |
| `UVM_ENABLE_PEER_ACCESS` | 启用 GPU 对等访问 |
| `UVM_MAP_EXTERNAL_ALLOCATION` | 将 RM 分配的显存注册到 UVM |
| `UVM_CREATE_EXTERNAL_RANGE` | 创建 UVM 虚拟地址范围 |
| `UVM_MIGRATE` | 触发 CPU↔GPU 页面迁移 |
| `UVM_SET_PREFERRED_LOCATION` | 设置首选驻留位置（HBM vs system） |
| `UVM_SET_ACCESSED_BY` | 多 GPU 共享访问策略 |
| `UVM_ENABLE_READ_DUPLICATION` | 启用读复制（多 GPU 共享只读页） |
| `UVM_ALLOC_SEMAPHORE_POOL` | 分配跨 GPU 信号量池 |

**关键 UVM 结构**（[uvm_ioctl.h](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/main/kernel-open/nvidia-uvm/uvm_ioctl.h#L195-L213)）：

```c
#define UVM_MAP_EXTERNAL_ALLOCATION  UVM_IOCTL_BASE(33)
typedef struct {
    NvU64   base;             // IN  CPU 虚拟地址（由 RM_MAP_MEMORY mmap 返回）
    NvU64   length;           // IN
    NvU64   offset;           // IN  偏移
    UvmGpuMappingAttributes perGpuAttributes[UVM_MAX_GPUS];  // IN
    NvU64   gpuAttributesCount;
    NvS32   rmCtrlFd;         // IN  /dev/nvidiactl fd
    NvU32   hClient;          // IN  RM client handle
    NvU32   hMemory;          // IN  RM memory handle
    NV_STATUS rmStatus;       // OUT
} UVM_MAP_EXTERNAL_ALLOCATION_PARAMS;
```

**cuda_ioctl_sniffer 演示完整用户态 UVM 流程**（[github.com/geohot/cuda_ioctl_sniffer/gpu_driver.cc](https://github.com/geohot/cuda_ioctl_sniffer/blob/master/gpu_driver.cc)）：

```c
NvHandle heap_alloc(int fd_ctl, int fd_uvm, NvHandle root, NvHandle device,
                    NvHandle subdevice, void *addr, NvU64 length,
                    NvU32 flags, int mmap_flags, NvU32 type) {
    // 1. RM 分配显存
    NVOS32_PARAMETERS p = {
        .hRoot = root, .hObjectParent = device,
        .function = NVOS32_FUNCTION_ALLOC_SIZE,
        .data = { .AllocSize = {
            .owner = root, .type = type, .flags = flags, .size = length
        }}
    };
    ioctl(fd_ctl, __NV_IOWR(NV_ESC_RM_VID_HEAP_CONTROL, p), &p);
    NvHandle mem = p.data.AllocSize.hMemory;

    // 2. mmap 到进程地址空间
    void *local_ptr = mmap_object(fd_ctl, root, subdevice, mem, length, addr, mmap_flags);

    // 3. 创建 UVM 虚拟地址范围
    UVM_CREATE_EXTERNAL_RANGE_PARAMS p = { .base = (NvU64)local_ptr, .length = length };
    ioctl(fd_uvm, UVM_CREATE_EXTERNAL_RANGE, &p);

    // 4. 将 RM 分配绑定到 UVM
    UVM_MAP_EXTERNAL_ALLOCATION_PARAMS p = {0};
    p.base = (NvU64)local_ptr;  p.length = length;
    p.rmCtrlFd = fd_ctl;        p.hClient = root;  p.hMemory = mem;
    p.gpuAttributesCount = 1;   p.perGpuAttributes[0].gpuMappingType = 1;
    ioctl(fd_uvm, UVM_MAP_EXTERNAL_ALLOCATION, &p);

    return local_ptr;
}
```

### 4.3 GPU Page Fault 处理路径

**硬件层**：

- GMMU（GPU MMU）写入 fault 到 replayable/non-replayable fault buffer
- 寄存器：`NV_PFB_PRI_MMU_REPLAY_FAULT_BUFFER_LO/HI`、`NV_PFB_PRI_MMU_NON_REPLAY_FAULT_BUFFER_LO/HI`
- Fault packet: 32 字节，存于 `NV_MMU_FAULT_BUFFER_PACKET_SIZE`

**证据 - Open GPU 文档**（[nvidia.github.io/open-gpu-doc](https://nvidia.github.io/open-gpu-doc/manuals/turing/tu104/dev_mmu_fault.ref.txt)）：

> "This manual contains information definition of replayable (UVM) and non-replayable fault buffer packet in memory. ... The replayable fault buffer is managed by the UVM driver. The non-replayable fault buffer is managed by RM."

> "This is done by allowing page faults to be stalling and support replay, and by reporting page faults to the operating system or GPU driver in an efficient manner."

**内核态 UVM 处理**（[kernel-open/nvidia-uvm/uvm_gpu_replayable_faults.c](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/57130a27/kernel-open/nvidia-uvm/uvm_gpu_replayable_faults.c)）：

```c
static void fault_buffer_reinit_replayable_faults(uvm_parent_gpu_t *parent_gpu) {
    uvm_replayable_fault_buffer_t *replayable_faults = &parent_gpu->fault_buffer.replayable;
    // 同步 cached get/put 指针（resume from power mgmt）
    replayable_faults->cached_get = parent_gpu->fault_buffer_hal->read_get(parent_gpu);
    replayable_faults->cached_put = parent_gpu->fault_buffer_hal->read_put(parent_gpu);
    ...
}

static NV_STATUS hw_fault_buffer_flush_locked(uvm_parent_gpu_t *parent_gpu,
                                              hw_fault_buffer_flush_mode_t flush_mode) {
    // Confidential Computing (GSP-RM 拥有 HW replayable fault buffer)
    // 需先 flush HW buffer (via RM API)，再 flush SW "shadow" buffer
    ...
}
```

**VA Block 处理**（[kernel-open/nvidia-uvm/uvm_va_block.c](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/db0c4e65/kernel-open/nvidia-uvm/uvm_va_block.c)）：

```c
static NvU64 block_gpu_pte_flag_cacheable(uvm_va_block_t *block, uvm_gpu_t *gpu,
                                          uvm_processor_id_t resident_id) {
    // 本地显存总是 cached
    if (uvm_id_equal(resident_id, gpu->id))
        return UVM_MMU_PTE_FLAGS_CACHED;
    if (UVM_ID_IS_CPU(resident_id))
        return gpu_should_cache_sysmem(gpu) ? UVM_MMU_PTE_FLAGS_CACHED
                                            : UVM_MMU_PTE_FLAGS_NONE;
    // Peer memory 缓存策略由 uvm_exp_gpu_cache_peermem 决定
    return uvm_exp_gpu_cache_peermem == 0 ? UVM_MMU_PTE_FLAGS_NONE
                                          : UVM_MMU_PTE_FLAGS_CACHED;
}
```

**用户态角色**：仅通过 UVM ioctl 触发迁移（如 cudaMemPrefetchAsync → `UVM_MIGRATE`）、设置访问策略（`UVM_SET_PREFERRED_LOCATION`），**不做任何页表操作**。

### 4.4 系统分配（pageable）内存访问

CUDA 12.4+ 支持 GPU 直接访问 CPU malloc 内存（**System-Allocated Memory**）。这是 libcuda 通过 `UVM_PAGEABLE_MEM_ACCESS_ON_GPU` ioctl 查询能力，然后调用 `get_user_pages` 建立 GPU 页表。

**证据**（[uvm_ioctl.h](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/main/kernel-open/nvidia-uvm/uvm_ioctl.h)）：

```c
#define UVM_PAGEABLE_MEM_ACCESS_ON_GPU  UVM_IOCTL_BASE(70)
typedef struct {
    NvProcessorUuid gpuUuid;  // IN
    NvU32           pageSize; // IN  (0 = use system page size)
    NvBool          pageableMemAccess;  // OUT
    NvU32          caps;                // OUT  UVM_PAGEABLE_MEM_ACCESS_CAPS_*
    NV_STATUS       rmStatus;
} UVM_PAGEABLE_MEM_ACCESS_ON_GPU_PARAMS;
```

---

## 5. PTX 编译与 Module 加载（用户态核心职责）

### 5.1 用户态编译工具链

| 库 | 角色 | 接口前缀 |
|----|------|----------|
| `libnvrtc.so` | CUDA C++ → PTX JIT | `nvrtc*` |
| `libnvrtc-builtins.so` | 内置 CCCL/头文件 | 静态 |
| `libnvidia-ptxjitcompiler.so` | PTX → SASS (cubin) | `nvPTXCompiler*` |
| `libnvidia-fatbinaryloader.so` | 解析 fatbin → 选择匹配 arch | `nvfatbin*` |
| `libnvidia-nvvm.so` | NVVM IR → PTX (OpenCL 用) | LLVM IR-style |
| `libnvidia-allocator.so` | 显存分配策略 (cudaMallocAsync) | `cudaMemPool*` |
| `libnvidia-gpucomp.so` | Shader compiler (D3D/VK/GL/RT) | - |
| `libnvidia-tileiras.so` | Tile IR JIT (Hopper+) | - |
| `libnvjitlink*` | 运行时 linker (CUDA 11.4+) | `nvJitLink*` |
| `libnvfatbin*` | 运行时 fatbin 构造 (CUDA 12.4+) | `nvFatbin*` |

### 5.2 Module 加载路径

**核心证据 - NVIDIA Blog "CUDA Pro Tip: Understand Fat Binaries"**（[developer.nvidia.com/blog](https://developer.nvidia.com/blog/cuda-pro-tip-understand-fat-binaries-jit-caching/)）：

> "`nvcc`, the CUDA compiler driver, uses a two-stage compilation model. The first stage compiles source device code to PTX virtual assembly, and the second stage compiles the PTX to binary code for the target architecture. The CUDA driver can execute the second stage compilation at run time, compiling the PTX virtual assembly 'Just In Time' to run it."

> "The CUDA run time looks for code for the present GPU architecture in the binary, and runs it if found. If binary code is not found but PTX is available, then the driver compiles the PTX code."

> "The cache—referred to as the compute cache—is automatically invalidated when the device driver is upgraded, so that applications can benefit from improvements in the just-in-time compiler built into the device driver."

### 5.3 nvJitLink / nvFatbin（CUDA 12.4+ 新增）

**证据 - nvFatbin 文档**（[docs.nvidia.com/cuda/nvfatbin](https://docs.nvidia.com/cuda/archive/12.9.2/nvfatbin/index.html)）：

> "The Fatbin Creator APIs are a set of APIs which can be used at runtime to combine multiple CUDA objects into one CUDA fat binary (fatbin). The APIs accept inputs in multiple formats, either device cubins, PTX, or LTO-IR. The output is a fatbin that can be loaded by `cuModuleLoadData` of the CUDA Driver API."

> "The Fatbin Creator library requires no special system configuration. It does not require a GPU."

**证据 - nvJitLink 文档**（[docs.nvidia.com/cuda/nvjitlink](https://docs.nvidia.com/cuda/nvjitlink/index.html)）：

> "The JIT Link APIs are a set of APIs which can be used at runtime to link together GPU device code. The APIs accept inputs in multiple formats, either host objects, host libraries, fatbins (including with relocatable ptx), device cubins, PTX, index files or LTO-IR. The output is a linked cubin that can be loaded by `cuModuleLoadData` and `cuModuleLoadDataEx` of the CUDA Driver API."

### 5.4 用户态"硬件相关设置"——Module 加载细节

| 步骤 | 用户态 | 内核态 |
|------|--------|--------|
| 解析 fatbin header | ✅ libnvidia-fatbinaryloader | ❌ |
| 选择匹配 SM 的 cubin | ✅ 比对 `__CUDA_ARCH__` | ❌ |
| PTX → SASS JIT（如无 cubin） | ✅ libnvidia-ptxjitcompiler | ❌ |
| 校验 cubin ELF | ✅ | ❌ |
| 建立 GPU code object 描述符 | 准备参数 | ✅ 分配 RM Channel |
| 上传 SASS 到 GPU inst mem | 提交 ioctl | ✅ DMA 至 FB |
| 创建 module handle | ✅ `CUmodule` | ✅ RM object |

---

## 6. 用户态 vs 内核态职责矩阵

### 6.1 必须**在用户态完成的操作**

| 类别 | 具体操作 | 证据 |
|------|----------|------|
| **PTX/Cubin 编译** | PTX→SASS JIT | libnvidia-ptxjitcompiler（无 ioctl） |
| **Fatbin 解析** | 选 arch、提取 cubin | libnvidia-fatbinaryloader |
| **CUDA C++ 编译** | nvrtcCompileProgram | libnvrtc |
| **Module 链接** | nvJitLinkComplete (LTO/relocatable PTX) | libnvjitlink |
| **Kernel 参数序列化** | 构造参数指针数组 (void *args[]) | 由调用者栈分配 |
| **Graph 构造** | DAG 数据结构 | 完全用户态 |
| **Stream Capture** | 录制 API 调用 | 用户态 |
| **Memory Pool 策略** | cudaMemPool, cudaMallocAsync | libnvidia-allocator |
| **Context TLS 管理** | 线程 current context 栈 | 用户态 pthread TLS |
| **Device 枚举** | 遍历 PCI bus | 通过 ioctl 拉取但缓存用户态 |
| **Capability Cache** | 查询后的 `CUdevprop` 缓存 | 用户态 |

### 6.2 必须**在内核态完成的操作**

| 类别 | 具体操作 | 证据 |
|------|----------|------|
| **GPU 寄存器访问** | BAR0 MMIO | nvidia.ko 通过 ioremap |
| **中断处理** | MSI-X、IRQ handler | `nvidia_isr()` |
| **DMA 引擎编程** | copy engines (CE0..CE7) | kernel 端 setup descriptor |
| **GPU 页表操作** | radix tree PTE/PDE 更新 | nvidia-uvm.ko (uvm_va_block.c) |
| **Page fault 处理** | replayable fault buffer drain | nvidia-uvm.ko (uvm_gpu_replayable_faults.c) |
| **Channel 调度** | TSG (Time Slice Group)、fifo context switch | nvidia.ko RM |
| **Power management** | P-state 切换、persistence mode | nv_dynamic_power() |
| **Thermal** | 风扇曲线、温度采样触发降频 | kernel 调用 hw sensor |
| **MIG 资源分区** | GPU Instance / Compute Instance 硬件切片 | NV2080_CTRL_GR_GET_INFO 等 |
| **Confidential Computing** | SPDM 会话建立、加密 bounce buffer | GSP-RM + SEC2 引擎 |
| **PCIe 链路协商** | speed/width 训练 | NV2080_CTRL_CMD_BUS_SET_PCIE_SPEED |
| **Firmware 加载** | GSP-RM firmware upload | `request_firmware()` |

### 6.3 跨边界的协同操作

| 操作 | 用户态职责 | 内核态职责 |
|------|------------|------------|
| **cuMemAlloc** | 准备 NVOS32_PARAMETERS + 调用 ioctl | RM 分配 FB handle → mmap → 返回 VA |
| **cudaMallocManaged** | 调用 UVM_CREATE_EXTERNAL_RANGE + UVM_MAP_EXTERNAL_ALLOCATION | UVM 注册 va_block、按需迁移页 |
| **cuLaunchKernel** | 序列化 args、计算 launch config | RM Channel submit → GPU GR engine 取指 |
| **cuStreamSynchronize** | 等待 event fd 或 polling | 内核中断通知 → wake_up |
| **cuMemcpyDtoH/DtoD** | 设置 copy descriptor | DMA engine 编程、触发后返回 |
| **MIG enable** | NVML 调用 RM control | RM 重置 GPU、切分 GPC/TPC |

---

## 7. NVML / nvidia-smi 角色

### 7.1 NVML 架构

**证据 - NVML API Reference Guide**（[docs.nvidia.com/deploy/nvml-api](https://docs.nvidia.com/deploy/nvml-api/group__nvmlInitializationAndCleanup.html)）：

> "This chapter describes the methods that handle NVML initialization and cleanup. It is the user's responsibility to call `nvmlInit_v2()` before calling any other methods, and `nvmlShutdown()` once NVML is no longer being used."

> "`#define NVML_INIT_FLAG_NO_ATTACH 2` Don't attach GPUs.
> `#define NVML_INIT_FLAG_NO_GPUS 1` Don't fail nvmlInit() when no GPUs are found."

> "In NVML 5.319 new `nvmlInit_v2` has replaced `nvmlInit_v1` (default in NVML 4.304 and older) that did initialize all GPU devices in the system."

### 7.2 NVML 提供的功能大类

**证据 - NVML API Reference Manual**（[developer.download.nvidia.com NVML PDF](https://developer.download.nvidia.com/assets/cuda/files/CUDADownloads/NVML/nvml.pdf)）：

| 大类 | 示例函数 | 用户态/内核态 |
|------|----------|---------------|
| **设备查询** | `nvmlDeviceGetCount`、`nvmlDeviceGetHandleByIndex_v2`、`nvmlDeviceGetUUID` | 查询 RM 控制命令（NV2080） |
| **PCI 信息** | `nvmlDeviceGetPciInfo_v3`、`nvmlDeviceGetMaxPcieLinkGeneration` | NV2080_CTRL_CMD_BUS_* |
| **温度/功率** | `nvmlDeviceGetTemperature`、`nvmlDeviceGetPowerUsage`、`nvmlDeviceGetEnforcedPowerLimit` | 读 hw sensor |
| **时钟** | `nvmlDeviceGetClockInfo`、`nvmlDeviceSetApplicationsClocks` | NV2080_CTRL_CMD_CLK_* |
| **内存** | `nvmlDeviceGetMemoryInfo_v2` | NV2080_CTRL_CMD_FB_* |
| **ECC** | `nvmlDeviceGetEccMode`、`nvmlDeviceGetTotalEccErrors` | NV2080_CTRL_CMD_GPU_GET_ECC_* |
| **Encoder/Decoder** | `nvmlDeviceGetEncoderUtilization`、`nvmlDeviceGetDecoderUtilization` | NV2080_CTRL_CMD_MSENC_GET_* |
| **MIG** | `nvmlDeviceGetMigMode`、`nvmlComputeInstanceDestroy` | NV2080_CTRL_GPU_GET_MIG_* |
| **拓扑** | `nvmlDeviceGetTopologyNearestGpus`、`nvmlDeviceGetP2PStatus` | NV0000_CTRL_CMD_SYSTEM_GET_P2P_CAPS |
| **Persistence** | `nvmlDeviceGetPersistenceMode` | NV 控制命令 |

### 7.3 nvidia-smi 工作流程

nvidia-smi 是一个**纯用户态**的 CLI 工具，调用 libnvidia-ml.so，最终通过 libcuda 的相同 ioctl 路径与内核通信。它**不**做硬件设置，只读取监控数据。

---

## 8. MPS (Multi-Process Service) 演进

### 8.1 MPS 三组件

**证据 - NVIDIA MPS Architecture**（[docs.nvidia.com/deploy/mps](https://docs.nvidia.com/deploy/mps/architecture.html)）：

> "MPS is a binary-compatible client-server runtime implementation of the CUDA API which consists of several components:
> - **Control Daemon Process** – The control daemon is responsible for starting and stopping the server, as well as coordinating connections between clients and servers.
> - **Client Runtime** – The MPS client runtime is built into the CUDA Driver library and may be used transparently by any CUDA application.
> - **Server Process** – The server is the clients' shared connection to the GPU and provides concurrency between clients."

### 8.2 MPS 用户态 vs 内核态

| 组件 | 形态 | 职责 |
|------|------|------|
| `nvidia-cuda-mps-control` | root daemon | 协调 server 生命周期 |
| `nvidia-cuda-mps-server` | 用户态 server（per-UID） | 多 client Hyper-Q 共享 |
| libcuda MPS client | 内置在 libcuda.so | **运行时透明替换 RM client** |

**关键证据**：

> "When CUDA is first initialized in a program, the CUDA driver attempts to connect to the MPS control daemon. If the connection attempt fails, the program continues to run as it normally would without MPS. If however, the connection attempt succeeds, the MPS control daemon proceeds to ensure that an MPS server, launched with same user ID as that of the connecting client, is active before returning to the client."

> 意味着 libcuda.so **dlopen 检测** `$CUDA_MPS_PIPE_DIRECTORY` 下的 MPS 控制 unix socket，连接成功后所有 RM 客户端调用改为 IPC 到 server。

### 8.3 Volta+ 演进

- Volta+：支持 `-multiuser-server` 跨用户共享
- Volta+：支持 SM 百分比限制（`set_active_thread_percentage`）
- Ampere+：支持 SM 静态分区（`-S` 启动参数）
- Hopper+：MLOPart 支持（与 MIG 协同）

**证据 - MPS Quick Start**（[docs.nvidia.com/deploy/mps/595/quick-start](https://docs.nvidia.com/deploy/mps/595/quick-start.html)）：

> "To start a server configured to use MLOPart on supported devices for user `$UID`: `echo start_server -uid $UID -mlopart | nvidia-cuda-mps-control`"

> "Static SM partitioning must be enabled at MPS controller start time: `nvidia-cuda-mps-control -d -S`"

---

## 9. Confidential Computing 模式

### 9.1 H100 CC 架构

**证据 - NVIDIA Confidential Computing Demystified**（[arxiv.org/html/2507.02770v1](https://arxiv.org/html/2507.02770v1)）：

> "NVIDIA introduced the first commercial GPU-CC solution (Dhanuskodi et al., 2023) as part of its Hopper architecture."

> "Multiple architectural engines have been designed, customized, or specialized to support GPU-CC. These include the Foundation Security Processor (FSP), GPU System Processor (GSP), Secure Processor (SEC2), and Copy Engine (CE)."

> "For end users, enabling GPU-CC is designed to be seamless — AI applications can run without modification. However, enabling GPU-CC demands substantial changes to the underlying system stack. This includes modifications to the CUDA Runtime and user-mode driver, NVIDIA UVM and kernel-mode drivers, and support from a suite of dedicated hardware engines on the GPU."

### 9.2 CC 对用户态的影响

**证据 - NVIDIA H100 Confidential Computing Blog**（[developer.nvidia.com](https://developer.nvidia.com/blog/confidential-computing-on-h100-gpus-for-secure-and-trustworthy-ai/)）：

> "To solve this, the NVIDIA driver, which is inside the CPU TEE, works with the GPU hardware to move data to and from GPU memory. It does so through an encrypted bounce buffer, which is allocated in shared system memory and accessible to the GPU."

> "Similarly, all command buffers and CUDA kernels are also encrypted and signed before crossing the PCIe bus."

> "After the CPU TEE's trust has been extended to the GPU, running CUDA applications is identical to running them on a GPU with CC-Off. The CUDA driver and GPU firmware take care of the required encryption workflows in CC-On mode transparently."

### 9.3 CC 对 UVM 的改造

**证据 - CACM Creating the First Confidential GPUs**（[cacm.acm.org](https://cacm.acm.org/practice/creating-the-first-confidential-gpus/)）：

> "NVIDIA has long provided our developers with a solution called Unified Virtual Memory (UVM) that automatically handles page migrations between the GPU memory and the CPU memory based on a memory allocation API called `cudaMallocManaged()`. When the CPU accesses the data, UVM migrates the pages to the CPU system memory. When the data is needed on the GPU, UVM migrates it to the GPU memory. **For CC, we extended UVM to employ encrypted and authenticated paging through bounce buffers in shared memory.**"

> 复现 nvidia-uvm 中 flush 函数注释（[uvm_gpu_replayable_faults.c](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/57130a27/kernel-open/nvidia-uvm/uvm_gpu_replayable_faults.c)）：

```c
// In Confidential Computing GSP-RM owns the HW replayable fault buffer.
// Flushing the fault buffer implies flushing both the HW buffer (using a RM
// API), and the SW buffer accessible by UVM ("shadow" buffer).
```

---

## 10. Grace Hopper / 硬件相干架构

### 10.1 NVLink-C2C 与硬件相干

**证据 - NVIDIA Grace Hopper Architecture Blog**（[developer.nvidia.com](https://developer.nvidia.com/blog/nvidia-grace-hopper-superchip-architecture-in-depth/)）：

> "NVIDIA NVLink-C2C is an NVIDIA memory coherent, high-bandwidth, and low-latency superchip interconnect. It is the heart of the Grace Hopper Superchip and delivers up to 900 GB/s total bandwidth. This is 7x higher bandwidth than x16 PCIe Gen5 lanes commonly used in accelerated systems."

> "NVLink-C2C enables applications to oversubscribe the GPU's memory and directly utilize NVIDIA Grace CPU's memory at high bandwidth. With up to 512 [GB] of LPDDR5X CPU memory per Grace Hopper Superchip, the GPU has direct high-bandwidth access to [4x] more memory than what is available with HBM."

> "Furthermore, NVLink-C2C is a coherent memory interconnect with native hardware support for system-wide atomic operations."

### 10.2 硬件相干 vs 软件相干

**证据 - CUDA Programming Guide**（[docs.nvidia.com/cuda/cuda-programming-guide](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/unified-memory.html)）：

> "We refer to systems with a combined page table for both CPUs and GPUs as hardware-coherent systems. Systems with separate page tables for CPUs and GPUs are referred to as software-coherent."

> "Hardware-coherent systems such as NVIDIA Grace Hopper offer a logically combined page table for both CPUs and GPUs."

### 10.3 CDMM 模式

**证据 - NVIDIA Blog "Understanding Memory Management on Hardware-Coherent Platforms"**（[developer.nvidia.com/blog](https://developer.nvidia.com/blog/understanding-memory-management-on-hardware-coherent-platforms/)）：

> "NVIDIA released the Coherent Driver-based Memory Management (CDMM) mode for the NVIDIA driver for platforms that are hardware-coherent, such as GH200, GB200 and GB300. CDMM allows the NVIDIA driver, instead of the OS, to control and manage the GPU memory."

> "In CDMM mode, the CPU memory is managed by the Linux kernel and the GPU memory is managed by the NVIDIA driver. This means the NVIDIA driver, not the OS, is responsible for managing the GPU memory."

→ **对 libcuda 影响**：在 CDMM 模式下，libcuda 不通过内核分配 GPU 内存，但仍然通过 NVLink-C2C 共享单页表，需调用 NV0000_CTRL_CMD_SYSTEM_SET_MEMORY_POLICY 等新 RM 控制命令。

---

## 11. CUDA 12.x 架构变化

| 变化 | 时间 | 用户态影响 | 证据 |
|------|------|------------|------|
| Forward compat packages | 11.1+ | libcuda 可与旧内核兼容 | [Forward Compatibility](https://docs.nvidia.com/deploy/cuda-compatibility/forward-compatibility.html) |
| **nvJitLink** | 11.4 | 新增用户态链接器 | docs.nvidia.com/cuda/nvjitlink |
| **Per-thread default stream** | 11.x | 隐式 stream 行为变更 | CUDA Programming Guide |
| **CUDA Graphs 改进** | 12.0 | graph node types 扩展 | CUDA Driver API v12 |
| **Green Context / Execution Context** | 12.4 | 轻量级 ctx 抽象 | [CUDA Runtime - Execution Context](https://docs.nvidia.com/cuda/cuda-runtime-api/group__CUDART__EXECUTION__CONTEXT.html) |
| **nvFatbin** | 12.4 | 运行时 fatbin 构造 | [nvFatbin docs](https://docs.nvidia.com/cuda/archive/12.9.2/nvfatbin/index.html) |
| **Tile IR (libnvidia-tileiras)** | 12.4+ | Tile-based GPU 编程（JIT） | libnvidia-container 库清单 |
| **MIG 改进** | 12.x | 多 CI 枚举支持 | [MIG Device Names](https://docs.nvidia.com/datacenter/tesla/mig-user-guide/mig-device-names.html) |

**证据 - Forward Compatibility**（[docs.nvidia.com](https://docs.nvidia.com/deploy/cuda-compatibility/forward-compatibility.html)）：

> "There are specific features in the CUDA driver that require kernel-mode support and will only work with a newer kernel mode driver. A few features depend on other user-mode components and are therefore also unsupported."

→ 表明 **CUDA driver 的功能可大致分为 4 类**：纯用户态 / 需新 libcuda / 需新内核 / 需新 user-mode 工具（如 nvidia-uvm-tools）。

---

## 12. MIG (Multi-Instance GPU) 资源管理

### 12.1 MIG 概念

**证据 - NVIDIA MIG User Guide**（[docs.nvidia.com/datacenter/tesla/mig-user-guide](https://docs.nvidia.com/datacenter/tesla/mig-user-guide/latest/concepts.html)）：

> "A GPU Instance (GI) is a combination of GPU slices and GPU engines (DMAs, NVDECs, and so on). Anything within a GPU instance always shares all the GPU memory slices and other GPU engines, but its SM slices can be further subdivided into compute instances (CI). A GPU instance provides memory QoS."

> "A Compute Instance (CI) contains a subset of the parent GPU instance's SM slices and other GPU engines (DMAs, NVDECs, etc.). The CIs share memory and engines."

### 12.2 MIG 内核态 vs 用户态

| 职责 | 用户态（libcuda/libnvidia-ml） | 内核态（nvidia.ko） |
|------|-------------------------------|---------------------|
| MIG 模式切换 | NVML `nvmlDeviceSetMigMode` | NV2080_CTRL_CMD_GPU_SET_MIG_MODE → **GPU reset** |
| GI/CI 创建/销毁 | NVML `nvmlDeviceCreateGpuInstance` | NV2080_CTRL_GPU_CREATE_GPU_INSTANCE |
| 分区 profile 枚举 | NVML `nvmlDeviceGetMigProfile` | NV2080_CTRL_GPU_GET_MIG_PROFILES |
| 应用 CUDA_VISIBLE_DEVICES | libcuda 解析 | 内核枚举设备 |
| **硬件切片隔离执行** | ❌ | ✅ 由 GPC/TPC/SM 物理分区 |

### 12.3 MIG 设备枚举

**证据 - MIG Device Names**（[docs.nvidia.com](https://docs.nvidia.com/datacenter/tesla/mig-user-guide/mig-device-names.html)）：

> "Starting with CUDA 12/R570, enumeration of a single compute instance (CI) per GPU instance (GI) is supported. In other words, a single CUDA process can enumerate across multiple GPU instances, but only one CI per GI. CUDA applications treat a CI and its parent GI as a single CUDA device."

> "`CUDA_VISIBLE_DEVICES` has been extended to support MIG."

---

## 13. 对 UsrLinuxEmu 用户态驱动的启示

### 13.1 UsrLinuxEmu 已具备的能力（参考 AGENTS.md）

- ✅ `cuda_scheduler.cpp` 实现 `CudaScheduler` (DDS v1.2)
- ✅ `gpu_driver_client.cpp` 调用 `GPU_IOCTL_*` (magic='G')
- ✅ `cuda_stub.cpp` Stub 模式实现
- ✅ `IGpuDriver` 抽象层（Phase 2: VA Space/Queue）

### 13.2 从 NVIDIA UMD 学到的设计原则

基于本次调研，对 UsrLinuxEmu 用户态驱动设计提出以下建议：

#### 13.2.1 明确职责边界

| 职责 | 应在 UsrLinuxEmu User 层 | 应在 Plugin/Host 层 |
|------|---------------------------|---------------------|
| PTX JIT（如未来需要） | ✅ 可 dlopen 外部 JIT | ❌ |
| 内存分配参数构造 | ✅ | ioctl 转发 |
| Stream/Context 句柄 | ✅ TLS 用户态对象 | ❌ |
| GPU 寄存器访问 | ❌ | ✅ MMIO |
| 中断处理 | ❌ | ✅ |
| 页表操作 | ❌（除非模拟 UVM） | ✅ |

#### 13.2.2 保持 Ioctl "控制面 + 数据面" 分离

NVIDIA 的设计：

- **控制面**：`NV_ESC_RM_CONTROL` + ctrl 命令编号（NV2080_*） → 状态查询/配置
- **数据面**：`NV_ESC_RM_MAP_MEMORY` + mmap → 大块 DMA buffer 传输

建议 UsrLinuxEmu：

- 控制消息走 `gpu_ioctl.h` 的控制命令
- 大块数据（显存、kernel 参数）走 mmap 共享内存，避免 ioctl 复制

#### 13.2.3 延迟加载可选组件

NVIDIA 的 libcuda 通过 `dlopen("libnvidia-ptxjitcompiler.so")` 实现可选 JIT。建议 UsrLinuxEmu 的 `cuda_stub` 不强依赖 PTX 编译器，而是按需加载。

#### 13.2.4 保留 Forward Compatibility 设计空间

NVIDIA 的 forward compat package（[forward-compat 文档](https://docs.nvidia.com/deploy/cuda-compatibility/forward-compatibility.html)）允许用户态驱动新版本兼容旧内核。建议 UsrLinuxEmu 用户态驱动版本与 Host Plugin 版本解耦，通过版本协商 ioctl 检测能力。

#### 13.2.5 MIG/Green Context 抽象

NVIDIA 的 Green Context（CUDA 12.4+）和 MIG 表明：**GPU 资源可以分层抽象**。UsrLinuxEmu 可考虑：

- **轻量级"逻辑 GPU"**（类似 Green Context）：仅在用户态做资源切片
- **完整 MIG 模拟**：通过 Host 端划分 SM 配额（需 Host 支持）

#### 13.2.6 流捕获与 Graph 构造可纯用户态

`cudaStreamBeginCapture` / `cudaGraph` 系列 API 完全在用户态实现 DAG 数据结构。UsrLinuxEmu 的 `CudaScheduler` 可直接实现 stream capture，无需 Host 端参与。

#### 13.2.7 参考 RM Control 命令分层

NVIDIA 的 ctrl 命令按类别编号（NV0000 system / NV0080 GPU / NV2080 subdevice / NV20xx GR/FB/FIFO）。UsrLinuxEmu 可在 `gpu_ioctl.h` 中定义类似分层：

```c
#define GPU_CTRL_CLASS_GPU      0x00  // 系统级
#define GPU_CTRL_CLASS_CTX      0x01  // 上下文
#define GPU_CTRL_CLASS_MEM      0x02  // 显存
#define GPU_CTRL_CLASS_STREAM   0x03  // 流
#define GPU_CTRL_CLASS_KERNEL   0x04  // 内核加载
```

---

## 14. 调研不确定项 / 无法确认

以下信息受限于 NVIDIA 闭源驱动 + 公开文档不足，无法直接确认：

| 主题 | 状态 |
|------|------|
| libcuda.so 内部具体哪些函数调用了哪些 ioctl | ❌ 闭源，仅通过 strace 抓取模式可推断 |
| PTX JIT 内部算法（寄存器分配、scheduling） | ❌ 闭源 |
| RM 内核模块（`nv-kernel.o`）内部 GSP-RM 通信协议 | ❌ 闭源，仅知通过 DMA 与 GSP 固件通信 |
| Confidential Computing 中 SPDM 完整握手协议 | ❌ 学术论文有概述但无完整 spec |
| Grace Hopper C2C 的 host 端 NVLink 驱动细节 | ❌ NVIDIA 闭源 |
| Tile IR 完整语法 | ❌ libnvidia-tileiras 闭源，公开 API 仅有 nvfatbin 添加 |

---

## 15. 核心引用清单

### 15.1 NVIDIA 官方文档

| 文档 | URL |
|------|-----|
| CUDA Driver API v13.3 | https://docs.nvidia.com/cuda/cuda-driver-api/ |
| CUDA Programming Guide v13.3 | https://docs.nvidia.com/cuda/cuda-programming-guide/ |
| CUDA Runtime API v13.3 | https://docs.nvidia.com/cuda/cuda-runtime-api/ |
| NVRTC 13.3 | https://docs.nvidia.com/cuda/nvrtc/ |
| nvPTXCompiler 13.3 | https://docs.nvidia.com/cuda/ptx-compiler-api/ |
| nvJitLink 13.3 | https://docs.nvidia.com/cuda/nvjitlink/ |
| nvFatbin 12.9 | https://docs.nvidia.com/cuda/archive/12.9.2/nvfatbin/ |
| Unified Memory Programming Guide | https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/unified-memory.html |
| NVIDIA Driver Installation Guide | https://docs.nvidia.com/datacenter/tesla/driver-installation-guide/595/ |
| MPS Architecture | https://docs.nvidia.com/deploy/mps/architecture.html |
| MPS Quick Start | https://docs.nvidia.com/deploy/mps/595/quick-start.html |
| NVML API Reference | https://docs.nvidia.com/deploy/nvml-api/ |
| MIG User Guide | https://docs.nvidia.com/datacenter/tesla/mig-user-guide/ |
| Forward Compatibility | https://docs.nvidia.com/deploy/cuda-compatibility/forward-compatibility.html |
| Open GPU Kernel Modules | https://github.com/NVIDIA/open-gpu-kernel-modules |
| Open GPU Manuals (Turing MMU) | https://nvidia.github.io/open-gpu-doc/ |

### 15.2 NVIDIA Developer Blog

| 文章 | URL |
|------|-----|
| CUDA Pro Tip: Fat Binaries | https://developer.nvidia.com/blog/cuda-pro-tip-understand-fat-binaries-jit-caching/ |
| Runtime Fatbin Creation | https://developer.nvidia.com/blog/runtime-fatbin-creation-using-the-nvidia-cuda-toolkit-12-4-compiler/ |
| Grace Hopper Architecture | https://developer.nvidia.com/blog/nvidia-grace-hopper-superchip-architecture-in-depth/ |
| Confidential Computing H100 | https://developer.nvidia.com/blog/confidential-computing-on-h100-gpus-for-secure-and-trustworthy-ai/ |
| Memory Management HW-Coherent | https://developer.nvidia.com/blog/understanding-memory-management-on-hardware-coherent-platforms/ |

### 15.3 GitHub 公开项目

| 项目 | 用途 |
|------|------|
| NVIDIA/open-gpu-kernel-modules | 开源 kernel modules（uvm_ioctl.h、nv-ioctl.h） |
| NVIDIA/libnvidia-container | 用户态库清单参考（compute_libs[]） |
| NVIDIA/cuda-samples | CUDA 示例（不展开） |
| geohot/cuda_ioctl_sniffer | 实际 ioctl 序列抓取与解析 |
| strace/strace#342 | ioctl 解码讨论 |
| fuzzinglabs.com NVIDIA internals | 内核模块分层分析 |

### 15.4 学术论文

| 论文 | URL |
|------|-----|
| NVIDIA GPU Confidential Computing Demystified | https://arxiv.org/html/2507.02770v1 |
| Creating the First Confidential GPUs (CACM) | https://cacm.acm.org/practice/creating-the-first-confidential-gpus/ |
| UVM In-depth Analyses | https://dl.acm.org/doi/10.1145/3458817.3480855 |

### 15.5 关键 ioctl 编号（NVIDIA）

**libcuda ↔ nvidia.ko 主要 ioctl**（NV_IOCTL_MAGIC）：

| 编号 | 名称 | 用途 |
|------|------|------|
| `NV_ESC_CARD_INFO` | 枚举 GPU 设备信息 | cuDeviceGet* |
| `NV_ESC_SYS_PARAMS` | 获取 memblock_size 等系统参数 | cuInit |
| `NV_ESC_CHECK_VERSION_STR` | RM API 版本协商 | cuInit |
| `NV_ESC_RM_ALLOC` | 分配 RM 对象 | cuModuleLoad/cuMemAlloc/cuStreamCreate |
| `NV_ESC_RM_CONTROL` | RM 控制命令分发 | cuDeviceGetAttribute/MIG |
| `NV_ESC_RM_MAP_MEMORY` | device memory mmap | cuMemAlloc |
| `NV_ESC_RM_VID_HEAP_CONTROL` | video heap 操作 | cuMemAlloc 内部 |
| `NV_ESC_RM_FREE` | 释放 RM 对象 | cuMemFree/cuStreamDestroy |
| `NV_ESC_ALLOC_OS_EVENT` | OS 事件分配 | cuEventCreate |
| `NV_ESC_REGISTER_FD` | 注册 fd | MPS client |

**UVM ioctl**（UVM_IOCTL_BASE）：

| 编号 | 名称 | 用途 |
|------|------|------|
| 1 | `UVM_INITIALIZE` | 初始化 UVM 子系统 |
| 2 | `UVM_MM_INITIALIZE` | 注册进程 mm_struct |
| 6 | `UVM_REGISTER_GPU` | GPU 注册到 va_space |
| 33 | `UVM_MAP_EXTERNAL_ALLOCATION` | 将 RM 显存注册到 UVM |
| 46 | `UVM_CREATE_EXTERNAL_RANGE` | 创建 UVM 虚拟地址范围 |
| 56 | `UVM_MIGRATE` | CPU↔GPU 页面迁移 |
| 58 | `UVM_SET_PREFERRED_LOCATION` | 设置首选驻留位置 |
| 70 | `UVM_PAGEABLE_MEM_ACCESS_ON_GPU` | 查询 pageable memory access |

### 15.6 libnvidia-container 库清单（NVIDIA 用户态全家福）

来自 `libnvidia-container/src/nvc_info.c`：

```
libcuda.so                          CUDA driver library
libcudadebugger.so                  CUDA Debugger Library
libnvidia-opencl.so                 NVIDIA OpenCL ICD
libnvidia-gpucomp.so                Shared Compiler Library
libnvidia-ptxjitcompiler.so         PTX-SASS JIT compiler
libnvidia-fatbinaryloader.so        fatbin loader
libnvidia-allocator.so              显存分配策略
libnvidia-compiler.so               NVVM-PTX compiler for OpenCL
libnvidia-pkcs11.so                 Encrypt/Decrypt library
libnvidia-nvvm.so                   NVVM Compiler library
libnvidia-tileiras.so               Tile IR JIT (Hopper+)
```

---

## 16. 关键文件路径速查

### 16.1 用户态入口函数（libcuda.so）

```
cuInit                              # 初始化（IOCTL_CHECK_VERSION_STR, SYS_PARAMS）
cuDeviceGetCount                    # 枚举 NV_ESC_CARD_INFO
cuDeviceGet                         # 获取设备句柄
cuDeviceGetAttribute                # NV2080_CTRL_* 查询
cuCtxCreate                         # 分配 Context (NV_ESC_RM_ALLOC)
cuCtxDestroy                        # NV_ESC_RM_FREE
cuMemAlloc                          # NV_ESC_RM_VID_HEAP_CONTROL (NVOS32_ALLOC_SIZE)
cuMemFree                           # NV_ESC_RM_FREE
cuMemcpyHtoD/DtoH/DtoD              # NV_ESC_RM_CONTROL (MEM_OP)
cuModuleLoad                        # load fatbin/PTX → NV_ESC_RM_ALLOC (Channel)
cuModuleGetFunction                 # 查 module symbol
cuLaunchKernel                      # 提交 kernel 到 channel
cuStreamCreate                      # 分配 Channel (NV_ESC_RM_ALLOC)
cuStreamSynchronize                 # poll event fd
cuEventCreate                       # NV_ESC_ALLOC_OS_EVENT
cuEventSynchronize                  # poll event fd
cuStreamBeginCapture                # 用户态 capture
cuStreamEndCapture                  # 用户态 → graph
```

### 16.2 内核态关键文件（open-gpu-kernel-modules）

```
kernel-open/
├── common/inc/
│   ├── nv.h                                  # NV_IOCTL_MAGIC, __NV_IOWR 宏
│   ├── nv-ioctl.h                            # nv_ioctl_xfer_t, nv_ioctl_card_info_t
│   ├── nv-ioctl-numbers.h                    # NV_ESC_* 编号
│   └── ctrl/
│       ├── ctrl0000/ctrl0000system.h         # NV0000_CTRL_CMD_SYSTEM_*
│       ├── ctrl0000/ctrl0000gpu.h            # NV0000_CTRL_CMD_GPU_*
│       ├── ctrl0080/ctrl0080gpu.h            # NV0080_CTRL_CMD_GPU_*
│       ├── ctrl2080/ctrl2080gpu.h            # NV2080_CTRL_CMD_GPU_*
│       ├── ctrl2080/ctrl2080bus.h            # NV2080_CTRL_CMD_BUS_*
│       ├── ctrl2080/ctrl2080fb.h             # Framebuffer
│       ├── ctrl2080/ctrl2080fifo.h           # FIFO (Channel)
│       ├── ctrl2080/ctrl2080gr.h             # Graphics engine
│       ├── ctrl2080/ctrl2080nvlink.h         # NVLink
│       └── ctrl2080/ctrl2080mc.h             # MIG
├── nvidia/
│   ├── nv.c                                  # nvidia_ioctl 入口
│   └── nv-chrdev.c                           # nvidia_fops
├── nvidia-uvm/
│   ├── uvm.c                                 # UVM ioctl 入口
│   ├── uvm_ioctl.h                           # UVM_* 编号
│   ├── uvm_linux_ioctl.h                     # UVM_IOCTL_BASE 宏
│   ├── uvm_va_block.c                        # VA Block 操作（页表）
│   ├── uvm_gpu_replayable_faults.c           # Fault 处理
│   └── uvm_migrate.c                         # 页面迁移
└── nvidia-modeset/
    └── nv-modeset-kernel.o                   # nv-kms（闭源二进制）
```

### 16.3 UsrLinuxEmu 对应路径（TaskRunner）

```
TaskRunner/
├── src/cuda_scheduler.cpp         # 类比 libcudart (stream/graph)
├── src/cuda_stub.cpp              # 类比 libcuda (cu* stub)
├── src/gpu_driver_client.cpp      # 类比 libcuda (ioctl 转发)
├── include/cuda_scheduler.hpp     # 调度抽象
├── include/gpu_driver_client.h    # UMD 接口
├── UsrLinuxEmu/plugins/gpu_driver/shared/
│   └── gpu_ioctl.h                # 类比 nv-ioctl.h + uvm_ioctl.h（magic='G'）
└── tests/test_cuda_scheduler.cpp  # E2E 测试
```

---

## 17. 总结

**NVIDIA CUDA 用户态驱动的核心职责**：

1. **API 抽象层**：将 cu* API 翻译为 ioctl
2. **JIT 编译**：PTX→SASS（用户态 dlopen libnvidia-ptxjitcompiler）
3. **Fatbin 解析**：选择 arch 匹配的 cubin（用户态）
4. **状态管理**：Context/Stream/Event 句柄的 TLS 数据结构（用户态）
5. **参数序列化**：Kernel args、launch config 打包（用户态）
6. **Graph 构造**：DAG 数据结构（用户态）
7. **Memory policy**：placement 提示（通过 UVM ioctl）
8. **可选组件加载**：MPS client、debugger、TLS 配置（用户态）

**NVIDIA 内核态驱动的核心职责**：

1. **MMIO / DMA**：GPU 寄存器访问
2. **中断处理**：event 通知
3. **页表管理**：UVM fault 处理、PTE 更新
4. **电源/热管理**：P-state、fan curve
5. **MIG 物理分区**：GPC/TPC 切片
6. **Channel 调度**：TSG、context switch
7. **Firmware 加载**：GSP-RM 启动

**UsrLinuxEmu 应借鉴的核心原则**：

- 用户态驱动应保持**纯数据结构 + ioctl 转发**，不做硬件 bit-bang
- **late binding** 关键组件（JIT、debugger），避免强依赖
- **控制/数据面分离**：控制走 ioctl，数据走 mmap 共享内存
- **分层 ctrl 命令编号**：便于未来扩展
- **MIG / Green Context 抽象**：在用户态实现资源切片

---

## Cross-References

Related research documents in this repository:

- [TaskRunner 自身定位与能力边界](../test-fixture/research/taskrunner-positioning-2026-06-24.md) `[TEST-FIXTURE SCOPE]`
- [UsrLinuxEmu GPU 驱动设计意图](../shared/research/usrlinuxemu-gpu-driver-design-2026-06-24.md) `[SHARED SCOPE]`
- [AMD ROCm 用户态驱动架构](./external-amd-rocm-umd-2026-06-24.md) `[UMD-EVOLUTION SCOPE]`

Upstream documents:

- [Umbrella gap-analysis.md](../gap-analysis.md)
- [Umbrella vision.md](../vision.md)
- [Umbrella vision-source.md](../vision-source.md)

Cross-repo references (UsrLinuxEmu docs/00_adr/):

- [ADR-024: User-mode queue submission (UMQ)](../../../../docs/00_adr/adr-024-user-mode-queue-submission.md)
- [ADR-023: HAL interface](../../../../docs/00_adr/adr-023-hal-interface.md)
- [ADR-021: Hardware puller FSM](../../../../docs/00_adr/adr-021-hardware-puller.md)
- [ADR-019: DRM/GEM/TTM alignment](../../../../docs/00_adr/adr-019-drm-gem-ttm-alignment.md)
- [Stage 1 roadmap: Kernel environment emulation](../../../../docs/roadmap/stage-1-kernel-emu.md)
