# Findings: TaskRunner-UsrLinuxEmu 接口分析（综合版）

## ⚠️ 重大发现：存在三套 ioctl 体系，严重的内部不一致

### 体系全貌

| 体系 | Magic | 用途 | 位置 | 状态 |
|------|-------|------|------|------|
| **A: CUDA 专用** | `'C'` | TaskRunner CudaStub 转译层 | `include/usr_linux_emu/cuda_ioctl.h` | ✅ 被 TaskRunner 引用 |
| **B: 旧版 GPGPU** | `'g'` | 内部 GPU 驱动仿真 | `drivers/gpu/ioctl_gpgpu.h` | ⚠️ legacy，逐步替换 |
| **C: 共享标准接口** | `'G'` | 与 TaskRunner 零耦合协同 | `plugins/gpu_driver/shared/gpu_ioctl.h` | 🔄 设计完成，待落地 |

### 关键问题

1. **TaskRunner `cmd_cuda.cpp` 调用 `CUDA_IOCTL_*` (magic='C')**
2. **但 `gpu_driver.cpp` 的 switch 处理的是 `GPGPU_*` (magic='g')**
3. **两者完全不兼容！** switch case 不会匹配！

### gpu_driver.cpp 实际处理的命令

```cpp
switch (request) {
    case GPGPU_GET_DEVICE_INFO: ...  // magic='g', cmd=0 ✅ 实现
    case GPGPU_ALLOC_MEM: ...         // magic='g', cmd=1 ⚠️ 线性分配
    case GPGPU_FREE_MEM: ...         // magic='g', cmd=2 ❌ 空实现
    case GPGPU_SUBMIT_PACKET: ...    // magic='g', cmd=5 ❌ 空实现(仅break)
}
```

### cuda_ioctl.h 定义的命令（TaskRunner 调用）

```cpp
CUDA_IOCTL_MEM_ALLOC      (0x01)  // gpu_driver 不处理！
CUDA_IOCTL_MEMCPY_H2D    (0x03)  // gpu_driver 不处理！
CUDA_IOCTL_LAUNCH_KERNEL (0x10)  // gpu_driver 不处理！
CUDA_IOCTL_WAIT_FENCE     (0x20)  // gpu_driver 不处理！
```

---

## 1. TaskRunner 项目分析

### 新增文件（与 UsrLinuxEmu 相关）

| 文件 | 说明 |
|------|------|
| `include/cmd_cuda.h` | CUDA CLI 命令接口 (DDS v1.1) |
| `include/cuda_scheduler.hpp` | CUDA 调度器头文件 (DDS v1.2) |
| `include/cuda_stub.hpp` | CUDA Stub（无 GPU 测试） |
| `include/memory_manager.hpp` | GPU 内存管理器 |
| `include/sync_primitives.hpp` | 同步原语（Barrier/Fence/Event） |
| `src/cuda_scheduler.cpp` | CUDA 调度器实现 |
| `src/cuda_stub.cpp` | CUDA Stub 实现 |
| `src/memory_manager.cpp` | 内存管理器实现 |
| `src/sync_primitives.cpp` | 同步原语实现 |
| `src/cmd_cuda.cpp` | **唯一调用 UsrLinuxEmu 的源文件** |
| `tests/test_cuda_scheduler.cpp` | CUDA 调度器测试 |

### 关键发现：src/cmd_cuda.cpp

```cpp
// line 22
#include "usr_linux_emu/cuda_ioctl.h"

// 使用的 ioctl 命令
CUDA_IOCTL_MEM_ALLOC      // 分配内存
CUDA_IOCTL_MEMCPY_H2D     // Host to Device 拷贝
CUDA_IOCTL_MEMCPY_D2H     // Device to Host 拷贝
CUDA_IOCTL_LAUNCH_KERNEL  // Kernel 启动
CUDA_IOCTL_WAIT_FENCE     // 等待 Fence
```

### TaskRunner 文档状态

| 文档 | 内容 | 与代码一致性 |
|------|------|--------------|
| `DOC-01-ioctl-api-spec.md` | 描述 `GPGPU_*` 命令 | ❌ 不一致（代码用 `CUDA_IOCTL_*`） |
| `DOC-02-device-command-protocol.md` | 描述 `GpuCommandPacket` 批量提交 | ⚠️ 文档有，代码未实现 |
| `DOC-03-command-translator.md` | 定义 TaskCommand → DeviceCommand 映射 | ⚠️ 文档有，CommandTranslator 未实现 |
| `DOC-04-ci-config.md` | CI/CD 和 submodule 策略 | ⚠️ submodule 未配置 |

---

## 2. UsrLinuxEmu 项目分析

### 关键发现：存在三套 ioctl 命名

| 文件 | 前缀 | Magic | 命令范围 |
|------|------|-------|---------|
| `include/usr_linux_emu/cuda_ioctl.h` | `CUDA_IOCTL_*` | `'C'` | 0x01-0x04, 0x10, 0x20-0x21, 0x30-0x31 |
| `drivers/gpu/ioctl_gpgpu.h` | `GPGPU_*` | `'g'` | 0x00, 0x01, 0x02, 0x05 |
| `docs/architecture_design.md` | `GPU_*` | `'G'` | 0x01-0x08（文档定义） |

### `cuda_ioctl.h` 定义 (TaskRunner 实际引用)

```cpp
#define CUDA_IOCTL_MAGIC 'C'

// 内存管理
#define CUDA_IOCTL_MEM_ALLOC      _IOWR('C', 0x01, struct cuda_mem_alloc_request)
#define CUDA_IOCTL_MEM_FREE       _IOWR('C', 0x02, struct cuda_mem_free_request)
#define CUDA_IOCTL_MEMCPY_H2D     _IOWR('C', 0x03, struct cuda_memcpy_h2d_request)
#define CUDA_IOCTL_MEMCPY_D2H     _IOWR('C', 0x04, struct cuda_memcpy_d2h_request)

// Kernel 启动
#define CUDA_IOCTL_LAUNCH_KERNEL  _IOWR('C', 0x10, struct cuda_launch_kernel_request)

// 同步原语
#define CUDA_IOCTL_WAIT_FENCE     _IOWR('C', 0x20, struct cuda_wait_fence_request)
#define CUDA_IOCTL_QUERY_FENCE    _IOWR('C', 0x21, struct cuda_query_fence_request)

// Phase 2 预留
#define CUDA_IOCTL_GRAPH_CREATE   _IOWR('C', 0x30, ...)
#define CUDA_IOCTL_GRAPH_LAUNCH   _IOWR('C', 0x31, ...)
```

**缺失**: `CUDA_IOCTL_GET_DEVICE_INFO` (0x00) 未定义

### `ioctl_gpgpu.h` 定义 (gpu_driver.cpp 实际使用)

```cpp
#define GPGPU_IOC_MAGIC 'g'

#define GPGPU_GET_DEVICE_INFO _IOR('g', 0, struct GpuDeviceInfo)
#define GPGPU_ALLOC_MEM      _IOWR('g', 1, struct GpuMemoryRequest)
#define GPGPU_FREE_MEM       _IOWR('g', 2, uint64_t)
#define GPGPU_SUBMIT_PACKET  _IOWR('g', 5, struct GpuCommandRequest)
```

**注意**: `gpu_driver.cpp` 使用 `GPGPU_*`，不是 `CUDA_IOCTL_*`！

### gpu_driver.cpp ioctl 处理 (lines 43-79)

```cpp
long GpuDriver::ioctl(int fd, unsigned long request, void* argp) {
    switch (request) {
        case GPGPU_GET_DEVICE_INFO: { ... }  // 使用 GPGPU_*
        case GPGPU_ALLOC_MEM: { ... }        // 使用 GPGPU_*
        case GPGPU_FREE_MEM: { ... }         // 使用 GPGPU_*
        case GPGPU_SUBMIT_PACKET: {           // 空实现！
            // 暂时忽略 ring buffer 相关的提交
            break;
        }
        default:
            std::cerr << "[GpuDriver] Unknown ioctl command: 0x" << std::hex << request << std::dec << std::endl;
            return -1;
    }
    return 0;
}
```

**严重问题**:
1. `gpu_driver.cpp` 处理的是 `GPGPU_*` (magic `'g'`)，不是 `CUDA_IOCTL_*` (magic `'C'`)
2. `GPGPU_SUBMIT_PACKET` 是空实现

### 结构体字段差异

| 结构体 | 文件 | 字段 |
|--------|------|------|
| `cuda_mem_alloc_request` | `cuda_ioctl.h` | `size`, `device_ptr`, `fence_id` |
| `GpuMemoryRequest` | `ioctl_gpgpu.h` | `size`, `space_type`, `phys_addr`, `cpu_ptr` |

---

## 3. 关键问题总结

### Q1: TaskRunner 调用哪个 ioctl？

**答案**: TaskRunner `cmd_cuda.cpp` 调用 `CUDA_IOCTL_*` (via `cuda_ioctl.h`)

**但**: UsrLinuxEmu `gpu_driver.cpp` 处理的是 `GPGPU_*`！

**结论**: 当前**无法对接**！两个项目的 ioctl 命令编号和 magic 完全不同。

### Q2: UsrLinuxEmu 事实上的接口是什么？

**候选 1**: `cuda_ioctl.h` (`CUDA_IOCTL_*`)
- 优点：被 TaskRunner 引用
- 缺点：`gpu_driver.cpp` 不处理

**候选 2**: `ioctl_gpgpu.h` (`GPGPU_*`)
- 优点：`gpu_driver.cpp` 实际处理
- 缺点：TaskRunner 代码不用这个

**结论**: 需要 UsrLinuxEmu 团队澄清

### Q3: 批量提交 (SUBMIT_PACKET) 状态？

**文档**: DOC-02 定义了 `GpuCommandPacket` 协议
**实现**: `gpu_driver.cpp` line 68-71 是**空实现**

---

## 4. 下一步行动

根据 Oracle 建议和上述分析：

1. **Phase 0 (审计)**: 确认 UsrLinuxEmu 到底用哪套接口
2. **Phase 1 (统一规范)**: 确定 `CUDA_IOCTL_*` 为标准
3. **Phase 2 (代码对齐)**: 修改 `gpu_driver.cpp` 处理 `CUDA_IOCTL_*`
4. **Phase 3 (Submodule)**: 配置 git submodule
5. **Phase 4 (文档)**: 更新所有文档

---

## 参考文件

### TaskRunner
- `/workspace/project/TaskRunner/src/cmd_cuda.cpp` - 唯一调用 UsrLinuxEmu 的源文件
- `/workspace/project/TaskRunner/docs/00_UsrLinuxEmu_Interface/DOC-01-ioctl-api-spec.md`
- `/workspace/project/TaskRunner/docs/00_UsrLinuxEmu_Interface/DOC-02-device-command-protocol.md`

### UsrLinuxEmu
- `/workspace/project/UsrLinuxEmu/include/usr_linux_emu/cuda_ioctl.h` - CUDA ioctl 定义
- `/workspace/project/UsrLinuxEmu/drivers/gpu/ioctl_gpgpu.h` - GPGPU ioctl 定义
- `/workspace/project/UsrLinuxEmu/drivers/gpu/gpu_driver.cpp` - ioctl 处理实现
- `/workspace/project/UsrLinuxEmu/docs/architecture_design.md` - 架构文档（过时）
