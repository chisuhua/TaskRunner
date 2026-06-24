 下面是本项目的架构文档，规划文档，请在docs/目录下创建对应内容的更清晰的文档，根据规划给出开发计划文档，我们讨论完开发计划文档后再进行一步一步实施。

1. **双运行时兼容**：同时支持 CUDA Runtime 和 Vulkan Runtime API
2. **三层解耦架构**：算法核心（平台无关）+ 调度层（TaskRunner 原始框架）+ 运行时（CUDA/Vulkan 兼容层）
3. **fused device 优化**：统一地址空间、CXL 协议集成、页迁移/缓存一致性仿真
4. **UsrLinuxEmu 协同**：驱动/硬件仿真代码分离至 UsrLinuxEmu 项目 (https://github.com/chisuhua/UsrLinuxEmu) 
5. **渐进式演进**：Phase 1 → Phase 3 可独立交付验证

---

## 一、整体架构蓝图

### 1.1 四层架构全景图

```mermaid
flowchart TD
    subgraph Application_Layer
        A[CUDA App<br>cudaLaunchKernel]
        B[Vulkan App<br>vkQueueSubmit]
    end
    
    subgraph Runtime_API_Layer
        C[CUDA Runtime<br>libcuda.so 仿真]
        D[Vulkan Runtime<br>VkInstance/VkDevice]
    end
    
    subgraph TaskRunner_Core_Layer
        E[CmdStream/CmdBuffer<br>原始调度框架]
        F[Pushbuffer Manager<br>GPFIFO 序列化]
        G[Firmware Decoder<br>固件命令解码]
        H[Resource Manager<br>Vk/CUDA 资源生命周期]
    end
    
    subgraph Algorithm_Core_Layer
        I[libgpu_core<br>MMU 事件处理]
        J[Buddy Allocator<br>纯地址运算]
        K[CXL Coherence FSM<br>MESI 状态机]
    end
    
    subgraph UsrLinuxEmu_Interface
        L[/dev/gpgpu0<br>ioctl 接口]
    end
    
    A --> C --> E
    B --> D --> E
    E --> F --> L
    E --> G --> L
    E --> H --> I
    I --> J
    I --> K
    
    classDef app fill:#e1f5ff,stroke:#0069d9
    classDef runtime fill:#bbdefb,stroke:#1976d2
    classDef core fill:#ffe0e0,stroke:#c62828
    classDef algo fill:#e8f5e9,stroke:#4caf50
    classDef interface fill:#ffe1e1,stroke:#ad1457
    class Application_Layer app
    class Runtime_API_Layer runtime
    class TaskRunner_Core_Layer core
    class Algorithm_Core_Layer algo
    class UsrLinuxEmu_Interface interface
```

### 1.2 核心架构原则

| 原则 | 说明 | 实现方式 |
|------|------|---------|
| **API 双兼容** | 同时支持 CUDA/Vulkan | 独立运行时层，共享核心调度 |
| **资源统一管理** | Vulkan/CUDA 资源生命周期 | `ResourceManager` 统一地址空间指针 |
| **同步模型融合** | `VkSemaphore` ↔ `cudaEvent` 双向映射 | 扩展 `Barrier` 支持跨 API 同步 |
| **固件可移植** | Device CPU 核固件代码 | `FirmwareDecoder` 100% 平台无关 |
| **算法可复用** | 页迁移/TLB 算法 | `libgpu_core/` 零系统调用 |

---

## 二、项目结构规划

```
TaskRunner/                          # 主项目根目录
├── CMakeLists.txt
├── README.md
├── LICENSE
│
├── include/                         # TaskRunner 原始 API（不变）
│   ├── taskrunner.h
│   └── taskrunner_fwd.h
│
├── src/                             # TaskRunner 原始实现（不变）
│   ├── taskrunner.cpp
│   ├── cmdstream.cpp
│   ├── cmdbuffer.cpp
│   ├── cmdprocessor.cpp
│   └── eventqueue.cpp
│
├── cuda_runtime/                    # ✅ CUDA Runtime 兼容层
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── cuda_runtime_api.h       # 标准 CUDA API 仿真
│   │   └── gpu_ioctl.h              # ioctl 编号（与 UsrLinuxEmu 共享）
│   └── src/
│       ├── libcuda_emu.cpp          # cudaLaunchKernel/cudaStreamCreate
│       └── pushbuffer_manager.cpp   # GPFIFO 序列化 + staging buffer
│
├── vulkan_runtime/                  # ✅ Vulkan Runtime 兼容层
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── vulkan_runtime_api.h     # Vulkan API 仿真
│   │   └── vk_types.h               # Vulkan 数据类型
│   └── src/
│       ├── vk_instance.cpp           # VkInstance/VkDevice 创建
│       ├── vk_command_buffer.cpp     # VkCommandBuffer 录制
│       ├── vk_queue.cpp              # VkQueue 提交
│       ├── vk_semaphore.cpp          # VkSemaphore/VkFence 实现
│       └── vk_resource_manager.cpp   # 资源生命周期管理
│
├── firmware/                        # ✅ Device 固件代码
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── firmware_decoder.h       # 命令解码接口
│   │   └── cpu_execution_unit.h     # work-stealing 任务执行
│   └── src/
│       ├── firmware_decoder.cpp     # GPFIFO entry → DecodedCommand
│       └── cpu_execution_unit.cpp   # Device CPU 任务池
│
├── libgpu_core/                     # ✅ 算法核心（100% 平台无关）
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── gpu_mmu_events.h         # 页迁移/TLB 事件模型
│   │   ├── gpu_buddy.h              # Buddy Allocator（纯地址运算）
│   │   └── gpu_cxl_fsm.h            # CXL.cache 状态机
│   └── src/
│       ├── mmu_events.cpp
│       ├── buddy.cpp
│       └── cxl_fsm.cpp
│
├── shared/                          # ✅ 跨项目共享接口
│   ├── gpu_regs.h                   # 寄存器偏移（硬件设计对齐）
│   ├── gpu_ioctl.h                  # ioctl 编号（TaskRunner + UsrLinuxEmu 共享）
│   ├── gpu_types.h                  # 跨平台数据类型
│   └── gpu_events.h                 # 事件类型定义（PAGE_INVALIDATE 等）
│
└── test/                            # 综合测试套件
    ├── test_cuda_api.cpp            # CUDA API 兼容性测试
    ├── test_vulkan_api.cpp          # Vulkan API 兼容性测试
    ├── test_interop.cpp             # Vulkan-CUDA 互操作测试
    ├── test_libgpu_core.cpp         # 算法核心单元测试
    └── test_portability.sh          # 仿真/真实驱动行为比对脚本
```

### 2.1 关键接口设计

#### 2.1.1 扩展 Barrier 支持双 API 同步

```cpp
// include/taskrunner/barrier.h (扩展)
#pragma once
#include <variant>
#include <memory>
#include <promise>

// 原始 BarrierType（保持兼容）
enum class BarrierType {
    RELEASE, ACQUIRE, WAIT, GROUP
};

// Vulkan 专用屏障类型
enum class VkBarrierType {
    SEMAPHORE_WAIT,
    SEMAPHORE_SIGNAL,
    IMAGE_MEMORY,
    BUFFER_MEMORY
};

// CUDA 专用屏障类型
enum class CudaBarrierType {
    EVENT_RECORD,
    EVENT_WAIT,
    STREAM_SYNCHRONIZE
};

// 扩展 Barrier 类（支持双 API）
class Barrier {
public:
    // 构造函数（保持原始 API 兼容）
    Barrier(BarrierType type, 
            std::shared_ptr<std::promise<void>> promise = nullptr,
            int groupId = 0);
    
    // Vulkan 扩展
    void set_vk_barrier(VkBarrierType vk_type, const void* data);
    
    // CUDA 扩展
    void set_cuda_barrier(CudaBarrierType cuda_type, uint64_t event_handle);
    
    // 获取屏障类型
    BarrierType get_base_type() const { return base_type_; }
    std::optional<VkBarrierType> get_vk_type() const;
    std::optional<CudaBarrierType> get_cuda_type() const;
    
private:
    BarrierType base_type_;
    std::shared_ptr<std::promise<void>> promise_;
    int group_id_;
    
    // API 特定数据
    std::variant<
        std::monostate,
        struct { VkBarrierType type; void* data; },
        struct { CudaBarrierType type; uint64_t handle; }
    > api_specific_data_;
};
```

#### 2.1.2 统一资源管理器

```cpp
// vulkan_runtime/include/vk_resource_manager.h
class ResourceManager {
public:
    // Vulkan 资源创建
    VkBuffer create_vk_buffer(VkDeviceSize size, VkBufferUsageFlags usage);
    VkImage create_vk_image(...);
    
    // CUDA 资源创建
    void* allocate_cuda_memory(size_t size);
    
    // 统一地址空间访问
    void* get_unified_address(uint64_t resource_handle);
    
    // 资源状态跟踪（Vulkan 专用）
    void update_image_layout(VkImage image, VkImageLayout new_layout);
    
    // 跨 API 互操作
    uint64_t import_vk_buffer_as_cuda(VkBuffer buffer);
    VkBuffer import_cuda_memory_as_vk(void* cuda_ptr, size_t size);
    
private:
    struct ResourceInfo {
        enum Type { VK_BUFFER, VK_IMAGE, CUDA_MEMORY } type;
        void* unified_ptr;  // 统一地址空间指针
        size_t size;
        // ... 其他元数据
    };
    
    std::unordered_map<uint64_t, ResourceInfo> resources_;
    std::atomic<uint64_t> next_handle_{1};
};
```

---

## 三、开发实施步骤计划

### Phase 1: 基础架构与 CUDA 兼容（3 个月）

| 里程碑 | 核心任务 | 交付物 | 验证指标 |
|--------|---------|--------|---------|
| **M1.1** | TaskRunner 原始框架集成 | `libtaskrunner_core.a` | 1. 通过原始 TaskRunner 测试套件<br>2. CmdStream/CmdBuffer 功能完整 |
| **M1.2** | CUDA Runtime 兼容层 | `libcuda_emu.so` | 1. `cudaLaunchKernel`/`cudaStreamCreate` 工作正常<br>2. 通过 vectorAdd/matrixMul 测试 |
| **M1.3** | Pushbuffer Manager 实现 | GPFIFO 序列化 + staging buffer | 1. GPFIFO entry 符合 NVIDIA 标准<br>2. 批量提交延迟降低 50%+ |
| **M1.4** | UsrLinuxEmu 接口集成 | `/dev/gpgpu0` ioctl 通信 | 1. ioctl 参数与 UsrLinuxEmu 完全一致<br>2. 命令提交成功率 100% |
| **M1.5** | 固件基础功能 | `FirmwareDecoder` 命令解码 | 1. GPFIFO entry → DecodedCommand 正确<br>2. 支持 OP_LAUNCH_KERNEL |

**Phase 1 交付物**：
- 完整的 CUDA Runtime 仿真环境
- 通过 NVIDIA CUDA sample 测试套件
- 与 UsrLinuxEmu 的稳定通信接口

---

### Phase 2: Vulkan 兼容与资源管理（2.5 个月）

| 里程碑 | 核心任务 | 交付物 | 验证指标 |
|--------|---------|--------|---------|
| **M2.1** | Vulkan Runtime 基础 | `VkInstance`/`VkDevice` 创建 | 1. `vkCreateInstance`/`vkCreateDevice` 成功<br>2. 队列族查询正确 |
| **M2.2** | 命令缓冲区支持 | `VkCommandBuffer` 录制/提交 | 1. `vkCmdDraw`/`vkCmdDispatch` 录制正确<br>2. 嵌套命令缓冲区支持 |
| **M2.3** | 多队列类型映射 | Graphics/Compute/Transfer 队列 | 1. 队列类型标签正确<br>2. 任务路由至合适处理器 |
| **M2.4** | 资源生命周期管理 | `ResourceManager` 统一管理 | 1. `vkCreateBuffer`/`vkDestroyBuffer` 正确<br>2. 统一地址空间指针有效 |
| **M2.5** | 同步原语实现 | `VkSemaphore`/`VkFence` | 1. `vkQueueSubmit` 正确处理信号量<br>2. 跨队列同步正确 |

**Phase 2 交付物**：
- 完整的 Vulkan Runtime 仿真环境
- 通过 Vulkan Triangle 示例
- 资源管理器统一地址空间验证报告

---

### Phase 3: 双 API 融合与高级特性（2.5 个月）

| 里程碑 | 核心任务 | 交付物 | 验证指标 |
|--------|---------|--------|---------|
| **M3.1** | Vulkan-CUDA 互操作 | 统一同步模型 | 1. `VkSemaphore` ↔ `cudaEvent` 双向映射<br>2. 跨 API 任务依赖正确执行 |
| **M3.2** | 算法核心集成 | `libgpu_core/` 页迁移仿真 | 1. `PAGE_INVALIDATE`/`PAGE_REMAP` 事件注入正确<br>2. TLB coherence 仿真准确 |
| **M3.3** | CXL.cache 仿真 | MESI 状态机集成 | 1. RdOwn/RdShared 事务处理正确<br>2. 缓存行无效化触发正确 |
| **M3.4** | 性能优化 | 批量提交 + 资源屏障验证 | 1. 提交延迟降低 70%+<br>2. 非法布局转换检测准确 |
| **M3.5** | 综合验证 | Vulkan CTS + CUDA Benchmarks | 1. 通过 Vulkan 1.3 CTS 核心测试<br>2. CUDA 性能接近原生 90%+ |

**Phase 3 交付物**：
- 完整的双 API 融合计算环境
- Vulkan CTS 通过报告
- 页迁移/缓存一致性仿真验证套件

---

## 四、关键架构优势总结

| 维度 | 传统方案缺陷 | 本方案解决方案 |
|------|-------------|---------------|
| **API 兼容性** | CUDA/Vulkan 分离实现 | 统一调度核心 + 独立运行时层 |
| **资源管理** | 资源生命周期分散 | `ResourceManager` 统一地址空间管理 |
| **同步模型** | 同步原语不互通 | 扩展 `Barrier` 支持跨 API 映射 |
| **算法复用** | 页迁移逻辑重复 | `libgpu_core/` 100% 平台无关 |
| **迁移路径** | 仿真/真实代码分离 | 通过 `test_portability.sh` 验证行为一致性 |

> **最终交付物**：  
> - ✅ **TaskRunner 项目**：提供 `libtaskrunner_core.a` + `libcuda_emu.so` + `libvulkan_emu.so`  
> - ✅ **双 API 兼容**：同时支持 CUDA 和 Vulkan 应用程序  
> - ✅ **统一资源管理**：Vulkan/CUDA 资源共享统一地址空间指针  
> - ✅ **可验证性保障**：`test_portability.sh` 确保仿真/真实驱动行为一致  
> - ✅ **平滑迁移路径**：算法核心和固件代码 100% 可复用于真实硬件  

该架构在**保持 TaskRunner 原始调度语义**的基础上，通过**最小侵入式扩展**实现双 API 兼容，为 CPU/GPU fused device 的异构计算提供完整解决方案。