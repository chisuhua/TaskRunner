# TaskRunner 新成员入职指南

> 生成时间: 2026-06-29  
> 基于提交: `a75f779`  
> 知识图谱: `.understand-anything/knowledge-graph.json`

---

## 目录

1. [项目概览](#1-项目概览)
2. [架构分层](#2-架构分层)
3. [关键概念](#3-关键概念)
4. [引导式导览](#4-引导式导览)
5. [文件地图（按层组织）](#5-文件地图按层组织)
6. [复杂度热点](#6-复杂度热点)
7. [快速上手指南](#7-快速上手指南)

---

## 1. 项目概览

| 属性 | 值 |
|------|-----|
| **项目名称** | TaskRunner |
| **语言** | C, C++, CMake, Markdown |
| **框架** | CMake, doctest |
| **C++ 标准** | C++17 |
| **构建系统** | CMake 3.20+ |
| **复杂度** | 小型（40 个追踪文件） |

### 一句话描述

基于 C++17 的并发任务执行框架，采用单例调度器（TaskRunner）配合多 CmdProcessor 工作线程和事件分发机制（dispatchLoop），支持任务队列、命令缓冲和栅栏同步。集成了 IGpuDriver GPU 驱动抽象层，分 `test-fixture`（可交付）和 `umd-evolution`（实验）两个 scope。

### H-5 三域架构

```
TaskRunner/
├── src/shared/                     # shared scope: 跨域共享抽象 (ACCEPTED)
├── src/test_fixture/               # test-fixture scope: 当前可交付状态 (ACCEPTED)
├── src/umd/                        # umd-evolution scope: 实验性演化层 (PROPOSED/DRAFT)
├── include/shared/
├── include/test_fixture/
└── include/umd/
```

每个 scope 有独立的 include/src/tests 目录，构建时通过 `TASKRUNNER_BUILD_MODE` 切换。

---

## 2. 架构分层

知识图谱分析识别出 7 个架构层：

### 2.1 构建基础设施

项目构建和版本控制配置：

- **CMakeLists.txt** — CMake 构建系统核心配置（C++17 标准、双模式选择、doctest 集成）
- **.gitignore** — Git 忽略规则（构建产物、临时文件、IDE 配置）
- **.gitmodules** — Git 子模块定义（doctest 测试框架）

### 2.2 项目文档

| 文件 | 内容 |
|------|------|
| README.md | 整体架构、详细设计、同步状态追踪 |
| AGENTS.md | Agent 开发指南（H-5 三域、构建命令、命名规范、跨仓同步） |
| CONTRIBUTING.md | 贡献指南（Directional Ownership、H-x 变更生命周期、ADR） |
| LICENSE | MIT 开源许可证 |

### 2.3 共享抽象层 (shared scope)

跨域共享的 GPU 抽象和基础设施，`ACCEPTED` 状态，双审机制：

- **IGpuDriver** — 统一的 GPU 驱动抽象接口（28+31 方法）
- **MemoryManager** — GPU 内存管理器（HOST_VISIBLE/DEVICE_LOCAL/MANAGED）
- **SyncPrimitives** — 分层同步原语（Barrier/Fence/Event）
- **ErrorHandling** — ErrorCode 枚举 + Result<T> 模板

### 2.4 测试夹具核心框架头文件 (test-fixture scope)

任务执行框架的核心 API 头文件，内部高度耦合（14 条内部 import 边）：

- **TaskRunner.h** — 单例调度器（dispatchLoop/launch/栅栏分配）
- **CmdProcessor.h** — 工作线程（eventLoop/任务偷取）
- **CmdBuffer.h** — 命令缓冲（有序/无序、Fence/Promise 管理）
- **CmdStream.h** — 命令流（事件生成）
- **EventQueue.h** — 事件队列（线程安全 push/pop）
- **TaskQueue.h** — FIFO 任务队列
- **TaskBuffer.h** — LIFO 任务缓冲
- **Barrier.h** — 栅栏类型（RELEASE/ACQUIRE/WAIT/GROUP）

### 2.5 测试夹具 GPU 集成头文件 (test-fixture scope)

GPU 驱动集成层：

- **CudaScheduler** — GPU 任务调度器（DI 注入 IGpuDriver/MemoryManager/SyncManager）
- **CudaStub** — Stub 模式实现（模拟 GPU 驱动行为用于开发测试）
- **GpuDriverClient** — System C 封装（GPU_IOCTL_* ioctl 封装）
- **cmd_cuda** — CLI 命令接口

### 2.6 测试夹具实现层 (test-fixture scope)

所有 `.cpp` 实现文件和示例入口，形成清晰的声明-实现分离模式：

- TaskRunner.cpp / CmdProcessor.cpp / cmd_buffer_v2.cpp
- cuda_scheduler.cpp / cuda_stub.cpp / gpu_driver_client.cpp
- cmd_cuda.cpp / cli_main.cpp / sample/main.cpp

### 2.7 UMD 演化实验层 (umd-evolution scope)

实验性 User Mode Driver 演化层，`PROPOSED/DRAFT` 状态，**自 2026-07-09 起默认编译**（`cmake -B build` 直接包含 libcuda_shim + tests/umd；可通过 `-DTASKRUNNER_BUILD_MODE=test-fixture` 排除）。与 test-fixture scope **非完全隔离**（CLI 仍依赖 `cuda_runtime_api.cpp`）：

- cuda_api — CUDA API 兼容层
- module_loader — 插件加载器
- ring_buffer — 环形缓冲

---

## 3. 关键概念

### 3.1 事件驱动调度

```
CmdStream → EventQueue → dispatchLoop → CmdProcessor → TaskQueue
                    ↑                             |
                    └─── processorToDispatch ──────┘
```

- **CmdStream** 生成事件（任务/命令缓冲/栅栏）推入 EventQueue
- **dispatchLoop** 统一轮询所有 EventQueue，以轮询方式分发
- **CmdProcessor** 从 dispatchToProcessorQueue 获取事件执行
- 处理结果通过 processorToDispatchQueue 回调 dispatchLoop

### 3.2 命令缓冲机制

- **有序模式**: 按 FIFO 顺序执行；遇到 ACQUIRE 栅栏暂停，RELEASE 栅栏恢复
- **无序模式**: 不保证执行顺序
- **Fence/Promise**: 通过 promise 实现组级别同步，checkFenceValue 定期检查恢复

### 3.3 任务窃取（Work Stealing）

CmdProcessor 在当前队列为空时，从其他 CmdProcessor 的 TaskQueue 窃取任务，实现负载均衡。

### 3.4 依赖注入（DI）

CudaScheduler 通过构造函数接收 IGpuDriver/MemoryManager/SyncManager 的实例，支持 Stub 模式和真实设备模式的无缝切换。

### 3.5 GPU 驱动契约

- 接口定义: `include/shared/igpu_driver.hpp`
- 真实实现: GpuDriverClient（通过 GPU_IOCTL_* ioctl 通信）
- 模拟实现: CudaStub（纯软件模拟用于测试）
- 使用 `GPU_IOCTL_*`（magic='G'）而非 `CUDA_IOCTL_*`（magic='C'）

### 3.6 跨仓同步协议

TaskRunner 是 UsrLinuxEmu 的 git submodule。改动时需同步：

1. TaskRunner 仓 → `git commit + git push`
2. UsrLinuxEmu 仓 → 更新 submodule 指针
3. 如新增 TADR-NNN → 更新 ADR-035 INDEX

---

## 4. 引导式导览

### Step 1: 项目概览与文档

从顶层文档开始，理解 TaskRunner 的整体架构设计哲学。

**推荐文件：** README.md, AGENTS.md, CONTRIBUTING.md, CMakeLists.txt

README 描述七大核心组件协作关系，AGENTS.md 记录 H-5 三域架构和跨仓同步规范（ADR-035）。

### Step 2: GPU 抽象接口层 (shared scope)

深入 shared scope 的核心抽象。

**推荐文件：** igpu_driver.hpp, memory_manager.hpp, sync_primitives.hpp, error_handling.hpp

IGpuDriver 是统一的 GPU 驱动接口（28+31 方法），定义了设备生命周期、缓冲管理、命令提交等关键契约。

### Step 3: 任务执行框架核心

TaskRunner 是整个框架的调度中枢。

**推荐文件：** TaskRunner.h, CmdStream.h, CmdBuffer.h, CmdProcessor.h

TaskRunner 采用单例模式，通过 dispatchLoop 统一轮询所有 CmdStream 事件队列，以轮询方式分发任务。

### Step 4: 任务队列与同步原语

辅助数据结构和同步机制。

**推荐文件：** EventQueue.h, TaskQueue.h, TaskBuffer.h, Barrier.h

这些组件是高度耦合的核心集群（14 条内部 import 边）。

### Step 5: GPU 调度器与依赖注入

CudaScheduler 是 GPU 任务调度的高层入口。

**推荐文件：** cuda_scheduler.hpp, cuda_stub.hpp, gpu_driver_client.h, cmd_cuda.h

通过依赖注入模式组合 IGpuDriver、MemoryManager 和 SyncManager。

### Step 6: Stub 驱动与 System C 封装

GPU 驱动的两种实现模式。

**推荐文件：** cuda_stub.cpp, gpu_driver_client.cpp, cuda_scheduler.cpp

CudaStub 用于脱离硬件开发测试，GpuDriverClient 与真实 GPU 驱动通信。

### Step 7: 工作线程与事件循环

CmdProcessor 是实际执行任务的工作线程。

**推荐文件：** CmdProcessor.cpp, TaskRunner.cpp

eventLoop 不断获取事件，处理有序/无序任务、栅栏同步和任务窃取。

### Step 8: CLI 接口与 CUDA 命令处理

CLI 模式提供交互式 GPU 命令执行入口。

**推荐文件：** cli_main.cpp, cmd_buffer_v2.cpp, cmd_cuda.cpp

### Step 9: 示例入口与内存管理实现

框架使用示例和共享层实现。

**推荐文件：** sample/main.cpp, memory_manager.cpp, sync_primitives.cpp

演示多线程任务执行、有序/无序命令缓冲、栅栏同步。

### Step 10: UMD 演化实验层与构建系统

实验性 scope 和构建配置。

**推荐文件：** cuda_runtime_api.hpp/cpp (Phase 1 CudaRuntimeApi)

实验性层**自 2026-07-09 起默认编译**（`cmake -B build`）；可通过 `-DTASKRUNNER_BUILD_MODE=test-fixture` 排除。

---

## 5. 文件地图（按层组织）

### 构建基础设施

| 文件 | 类型 | 说明 |
|------|------|------|
| `CMakeLists.txt` | config | CMake 构建系统（C++17、双模式、doctest 集成） |
| `.gitignore` | config | Git 忽略规则 |
| `.gitmodules` | config | Git 子模块（doctest） |

### 项目文档

| 文件 | 类型 | 说明 |
|------|------|------|
| `README.md` | document | 项目整体架构和同步状态 |
| `AGENTS.md` | document | Agent 开发指南（构建命令、命名规范、跨仓同步） |
| `CONTRIBUTING.md` | document | 贡献指南（Directional Ownership、H-x 生命周期） |
| `LICENSE` | document | MIT 开源许可证 |

### 共享抽象层 (shared scope)

| 文件 | 类型 | 复杂度 | 说明 |
|------|------|--------|------|
| `include/shared/igpu_driver.hpp` | code | moderate | 统一 GPU 驱动接口（31 个纯虚方法） |
| `include/shared/memory_manager.hpp` | code | moderate | GPU 内存管理器 |
| `include/shared/sync_primitives.hpp` | code | moderate | 分层同步原语（Barrier/Fence/Event） |
| `include/shared/error_handling.hpp` | code | moderate | ErrorCode 枚举 + Result<T> 模板 |
| `src/shared/memory_manager.cpp` | code | moderate | MemoryManager 实现 |
| `src/shared/sync_primitives.cpp` | code | moderate | SyncManager 实现 |

### 测试夹具核心框架头文件

| 文件 | 类型 | 复杂度 | 说明 |
|------|------|--------|------|
| `include/test_fixture/TaskRunner.h` | code | moderate | 单例调度器核心（459 行） |
| `include/test_fixture/CmdBuffer.h` | code | moderate | 命令缓冲（193 行） |
| `include/test_fixture/CmdProcessor.h` | code | moderate | 命令处理器（58 行） |
| `include/test_fixture/CmdStream.h` | code | moderate | 命令流（69 行） |
| `include/test_fixture/EventQueue.h` | code | moderate | 事件队列（57 行） |
| `include/test_fixture/TaskQueue.h` | code | moderate | 任务队列（64 行） |
| `include/test_fixture/TaskBuffer.h` | code | moderate | 任务缓冲（39 行） |
| `include/test_fixture/Barrier.h` | code | moderate | 栅栏定义（21 行） |

### 测试夹具 GPU 集成头文件

| 文件 | 类型 | 复杂度 | 说明 |
|------|------|--------|------|
| `include/test_fixture/gpu_driver_client.h` | code | **complex** | GpuDriverClient 类（577 行，31 方法） |
| `include/test_fixture/cuda_scheduler.hpp` | code | moderate | CudaScheduler 调度器（222 行） |
| `include/test_fixture/cuda_stub.hpp` | code | moderate | CudaStub 实现（230 行） |
| `include/test_fixture/cmd_cuda.h` | code | moderate | CUDA CLI 命令接口（53 行） |

### 测试夹具实现层

| 文件 | 类型 | 复杂度 | 说明 |
|------|------|--------|------|
| `src/test_fixture/cuda_stub.cpp` | code | **complex** | Stub 模式实现（503 行，19 个函数） |
| `src/test_fixture/cmd_cuda.cpp` | code | **complex** | CUDA 命令处理（408 行，9 个函数） |
| `src/test_fixture/cuda_scheduler.cpp` | code | **complex** | CUDA 调度器实现（368 行，9 个函数） |
| `src/test_fixture/CmdProcessor.cpp` | code | **complex** | 工作线程实现（195 行，6 个函数） |
| `src/test_fixture/cmd_buffer_v2.cpp` | code | moderate | V2 命令缓冲（203 行） |
| `src/test_fixture/cli_main.cpp` | code | simple | CLI 入口（65 行） |
| `src/test_fixture/gpu_driver_client.cpp` | code | simple | GpuDriverClient 封装（35 行） |
| `src/test_fixture/TaskRunner.cpp` | code | simple | 单例实现（9 行） |
| `sample/main.cpp` | code | moderate | 框架示例入口（112 行） |

### UMD 演化实验层

| 文件 | 类型 | 复杂度 | 说明 |
|------|------|--------|------|
| `include/umd/cuda_runtime_api.hpp` | code | medium | CudaRuntimeApi (Phase 1) |
| `src/umd/cuda_runtime_api.cpp` | code | medium | CudaRuntimeApi 实现 |

---

## 6. 复杂度热点

以下文件具有 **complex** 复杂度，新成员应谨慎对待：

| 文件 | 行数 | 说明 |
|------|------|------|
| `include/test_fixture/gpu_driver_client.h` | 577 | GpuDriverClient 类（31 个方法，System C ioctl 封装的全集） |
| `src/test_fixture/cuda_stub.cpp` | 503 | CudaStub 实现（模拟全部 IGpuDriver 方法） |
| `src/test_fixture/cmd_cuda.cpp` | 408 | CUDA CLI 命令处理（9 个函数处理各类 GPU_IOCTL_* 命令） |
| `src/test_fixture/cuda_scheduler.cpp` | 368 | CudaScheduler 调度器实现（任务调度完整流水线） |
| `src/test_fixture/CmdProcessor.cpp` | 195 | CmdProcessor 工作线程（事件循环、任务偷取、栅栏处理） |

建议从 `simple` 文件开始理解，逐步深入到 `moderate` 文件，最后挑战 `complex` 文件。

---

## 7. 快速上手指南

### 构建

```bash
# 默认构建（自 2026-07-09 起为 umd-evolution 模式，包含 libcuda_shim + tests/umd）
mkdir -p build && cd build && cmake .. && make -j4

# CLI 模式
cmake .. -DBUILD_CLI=ON && make -j4

# test-fixture opt-out（排除 libcuda_shim + tests/umd）
cmake .. -DTASKRUNNER_BUILD_MODE=test-fixture && make -j4

# UMD 显式（与默认行为等价，保留向后兼容）
cmake .. -DTASKRUNNER_BUILD_MODE=umd-evolution && make -j4
```

### 运行

```bash
# 运行测试
./build/test_cuda_scheduler

# 运行 CLI
./build/taskrunner cuda_alloc 4096
```

### 调试

```bash
# 初始化 LSP
./init.sh 或 ~/.config/opencode/scripts/init-clangd.sh .
```

### 学习路径建议

1. **先读文档** — README.md → AGENTS.md → CONTRIBUTING.md
2. **再读抽象层** — igpu_driver.hpp → memory_manager.hpp → sync_primitives.hpp → error_handling.hpp
3. **核心框架** — TaskRunner.h → CmdBuffer.h → CmdStream.h → EventQueue.h → Barrier.h → TaskQueue.h → CmdProcessor.h
4. **GPU 集成** — cuda_scheduler.hpp → cuda_stub.hpp → gpu_driver_client.h
5. **实现深入** — 从 simple → moderate → complex 逐步深入 `.cpp` 文件
6. **示例入口** — sample/main.cpp 作为框架使用的完整示例
7. **实验层** — umd-evolution scope 作为扩展阅读
8. **构建配置** — CMakeLists.txt + cmake/ 目录下的模块文件

---

> 本指南由 TaskRunner 知识图谱自动生成。运行 `/understand` + `/understand-onboard` 可重新生成。