---
SCOPE: SHARED
STATUS: ACCEPTED
DATE: 2026-06-24
RESEARCH_ID: bg_4dc24794
SOURCE_REPO: UsrLinuxEmu (submodule of TaskRunner)
RELATED_RESEARCH:
  - ../../umd-evolution/research/external-nvidia-cuda-umd-2026-06-24.md
---

# UsrLinuxEmu GPU 驱动设计意图调研

> **调研日期**：2026-06-24
> **调研方法**：深度阅读 13 份 UsrLinuxEmu ADR + 5 份路线图文档 + PRD + 5 份 TaskRunner TADR + GPU 驱动源码（`plugins/gpu_driver/plugin.cpp`、`drv/gpgpu_device.h`、`shared/gpu_ioctl.h`），所有结论带 ADR 编号 + 文件路径 + 行号。
> **目标**：回答 7 个核心问题——UsrLinuxEmu 定位、IOCTL 接口边界、用户态驱动位置、硬件相关设置归属、TaskRunner 角色、AMD/NVIDIA 借鉴、6-12 月演化方向。

---

## 执行摘要

- **定位**：UsrLinuxEmu 是 **"用户态 Linux 内核模拟环境"**（emulator 模拟 Linux 内核态驱动开发环境），不是 GPU 模拟器（≠QEMU）、不是性能对标平台。核心哲学：**"开发在用户态，部署到内核。仿真即验证，验证即迁移"**（PRD §1.1, ADR-001:18, ADR-036:17）。
- **3 区分原则**：① kernel env sim（`src/kernel/`, `include/linux_compat/`）— ② portable drv（`plugins/gpu_driver/drv/`）— ③ hardware sim（`plugins/gpu_driver/sim/`），HAL 桥（11 ops，ADR-023）作为 ②→③ 反向依赖注入点（ADR-036 §Decision）。
- **IOCTL 边界**：System C（`GPU_IOCTL_*` magic='G'）是用户空间可见的 **唯一 canonical 接口**，15 个命令分布在 5 个范围（0x01-0x03 命令提交、0x10-0x13 BO 管理、0x30-0x32 VA Space、0x40-0x43 UMQ Queue、GET_DEVICE_INFO）。Doorbell 通过 mmap 暴露用户态直写以支持零 syscall 提交（ADR-024 §决策 1）。
- **"用户态驱动 vs 内核驱动"边界**：UsrLinuxEmu **没有**该对立——它本身就是"用户态内核模拟环境"。`drv/` 写的是内核态驱动代码（在用户态仿真运行 + linux_compat + 10 条可移植性约束）。TaskRunner 是 IGpuDriver 抽象的 consumer，不是用户态驱动。
- **硬件相关设置归属**：严格按 3 区分原则分布在 drv/ + HAL + sim/ 三者，HAL 桥（11 ops）是核心机制。**没有"硬件相关设置都应集中在用户态驱动"的简单答案**——论断仅约 40% 正确（Ring Buffer / Doorbell / GPFIFO entry / user-mode fence 4 项），60% 偏差（PCIe/MSI-X/IOMMU/DRM/GEM/TTM/Power/Firmware/ATS 必须内核驱动）。
- **TaskRunner 5 角色**：测试驱动（GpuDriverClient/CudaStub/MockGpuDriver 3 个 IGpuDriver 实现）+ 调度器原型（CudaScheduler DI）+ CLI 工具（6 subcommand）+ 跨仓架构验证（31 测试用例）+ 可移植性 PoC。**未来蓝图不演化为真实生产用户态驱动**（如 NVIDIA cuStream）。
- **路线图 6-12 月**：大概率走 H-3.5 follow-up + H-7 上游问题解决 + Phase 3 Multi-GPU。中等概率：Stage 1.4 集成验证后成为 KFD 1 级 consumer。**明确不演化**：真实生产用户态驱动、多 API 完整 Runtime。

---

## 1. UsrLinuxEmu 定位（核心问题 1）

### 1.1 定位判定

**定位**：**"用户态 Linux 内核模拟环境"**——模拟**内核态驱动开发环境**，不是"模拟器（emulator）模拟一台机器"，不是"仿真器（simulator）仿真 GPU 微架构"。

| 来源 | 关键措辞 | 位置 |
|------|---------|------|
| ADR-001（2025-12，最早决策） | "采用**用户态模拟方案**，在用户空间实现 Linux 内核设备驱动框架的模拟" | `docs/00_adr/adr-001-user-mode-emulation.md:18` |
| ADR-001 §理由 | "降低开发门槛 / 提高安全性 / 便于调试 / 快速迭代 / **跨平台潜力**" | `adr-001:21-26` |
| ADR-001 §后果 | "⚠️ 有一定的性能开销（预计 **20-40%**）" | `adr-001:32` |
| PRD §1.1 一句话 | "**用户态 Linux 内核模拟环境**，让驱动开发者在**无需 root 权限、无需内核编译**的情况下开发、验证、调试设备驱动" | `docs/PRD.md:17` |
| PRD §1.3 产品定位 | "**不是**完整 GPU 模拟器（不等同于 QEMU 设备模拟）/ **不是**性能对标平台 / **是**驱动逻辑正确性验证平台 / **是**可移植内核驱动代码的开发环境" | `docs/PRD.md:30-35` |
| ADR-036 §Context | "UsrLinuxEmu 的核心目标是让驱动开发者在无需 root 权限或内核编译的情况下开发与测试 GPGPU 驱动，**最终目标是让验证过的驱动无痛迁移到真实 Linux 内核**" | `docs/00_adr/adr-036-three-way-separation.md:17` |
| Roadmap README §项目目标 | "**开发一个易移植到 Linux 内核的 GPU 驱动**" | `docs/roadmap/README.md:3` |

### 1.2 UsrLinuxEmu 在"模拟什么"？

UsrLinuxEmu 模拟的是**"Linux 内核运行时 + 真实 GPU 硬件的复合体"**，按 ADR-036 拆为：

- **模拟层 ①**（`src/kernel/`, `include/kernel/`, `include/linux_compat/`）= 模拟 Linux 内核 API
- **真实部分 ②**（`plugins/gpu_driver/drv/`）= **不是**模拟，而是**可移植的真实驱动代码**（目标：将来可零修改编译进真实内核）
- **模拟层 ③**（`plugins/gpu_driver/sim/`，含 `libgpu_core/`）= 模拟 GPU 硬件（puller / scheduler / buddy / doorbell）
- **HAL 桥**（`plugins/gpu_driver/hal/`，11 个 fn-ptr）= ② 调用 ③ 的依赖反向注入点

来源：ADR-036 §Decision 核心原则表格，`adr-036:35-41`。

### 1.3 目标用户

PRD §3 明确 4 类目标用户（`docs/PRD.md:96-101`）：

| 角色 | 关注点 | 使用方式 |
|------|--------|----------|
| **GPU 驱动开发工程师** | 驱动逻辑是否正确、是否可零修改移植到内核 | 在 UsrLinuxEmu 中开发 `drv/` 代码 |
| **TaskRunner 团队** | IOCTL 接口是否一致、能否提交命令到 `/dev/gpgpu0` | 通过 `GPU_IOCTL_*` 调用仿真设备 |
| **硬件仿真工程师** | 硬件行为是否被正确仿真 | 在 `sim/` 层实现硬件状态机 |
| **CI/CD 系统** | 每次提交是否通过回归测试 | 运行 `ctest` + 单元测试 |

---

## 2. 3-way 分离原则（核心原则）

### 2.1 核心原则表格

来源：ADR-036 §Decision 核心原则，`adr-036:35-41`：

| 编号 | 层次 | 物理路径 | 职责 |
|------|------|---------|------|
| **①** | **Linux 内核环境模拟** | `src/kernel/`, `include/kernel/`, `include/linux_compat/` | 提供 Linux 内核 API（VFS、调度、IOMMU、mmu_notifier、DRM、PCIe、中断） |
| **②** | **可移植的驱动代码实现** | `plugins/gpu_driver/drv/` | GPGPU 驱动逻辑（KFD 风格），用真实 Linux 内核 API 写 |
| **③** | **硬件模拟** | `plugins/gpu_driver/sim/`（含 `libgpu_core/`） | 模拟真实 GPU 硬件（pushbuffer、调度器、寄存器、fence、中断） |
| **HAL** | **桥（bridge）** | `plugins/gpu_driver/hal/`（11 个 fn-ptr） | ② 调 ③ 的依赖反向注入点 |

### 2.2 依赖方向规则

来源：ADR-018 决策 2 依赖方向，`adr-018:72-88` + ADR-036 §依赖规则，`adr-036:52-67`：

```
① (kernel env sim) ── dlopen ──> ② (drv/) ── 调用 ──> HAL (ops 表) ── 反向调用 ──> ③ (sim/)
```

- 任何被**用户态应用**直接调用的能力 → 必须经 ioctl 或 mmap 暴露
- 任何"驱动内部状态"或"硬件访问" → 仅在 drv/ 内通过 HAL 调用

### 2.3 drv/ 代码风格约束（10 条）

来源：PRD §6.1 可移植性约束，`docs/PRD.md:154-165`：

```
1. 不使用 C++ STL 容器
2. 不使用 C++ 异常（-fno-exceptions 必须能编译）
3. 不使用 RTTI
4. 不使用 std::cout / iostream
5. 不使用 std::thread / std::mutex
6. 所有硬件访问通过 HAL 接口
7. 使用 linux_compat 类型
8. 返回 Linux 错误码
9. 不使用全局/静态非平凡构造
10. 允许 C++ class、继承、虚函数、constexpr、namespace
```

→ **这 10 条规则明确证明：`drv/` 写的是内核态驱动代码**，不是用户态驱动。

---

## 3. HAL 桥机制（11 个 ops）

### 3.1 HAL 接口清单

来源：ADR-023 §决策 1 表格，`adr-023:48-62`：

| HAL 接口 | 用户态实现（sim/） | 内核态实现（真硬件） | 用户态可调？ |
|---------|------------------|-------------------|------------|
| `register_read` / `register_write` | 调用 DoorbellEmu/PcieEmu | `readl()` / `writel()` | ❌ 全部经 HAL |
| `mem_read` / `mem_write` | 读/写 sim 设备内存 | `memcpy_fromio()` / `memcpy_toio()` | ❌ |
| `mem_alloc` / `mem_free` | `SimBuddyAllocator` | `ttm_bo_allocate()` | ❌ |
| `doorbell_ring` | 触发 DoorbellEmu | `writel(1, doorbell_reg)` | ⚠️ **可经 mmap 直写**（ADR-024 快速路径） |
| `interrupt_raise` | callback | MSI-X 寄存器 + raise IRQ | ❌ |
| `fence_create` / `fence_read` | sim 变量 | 硬件 semaphore 寄存器 | ⚠️ fence_read 可经 mmap 轮询 |
| `time_wait` | `std::this_thread::sleep_for` | `usleep_range()` | ❌ |

### 3.2 HAL 反向依赖注入

HAL 是 **drv/ 调用 sim/ 的反向依赖注入点**：

- `drv/` 不直接 include `sim/` 头文件（编译期隔离）
- HAL ops 是函数指针表，由 `plugin_init_internal()` 在运行时注入
- 来源：`plugins/gpu_driver/plugin.cpp:30-99`

```cpp
static int plugin_init_internal() {
  static HalHolder hal_holder;
  hal_user_init(&hal_holder.hal, &hal_holder.ctx);          // HAL 注入
  hal_holder.puller = std::make_shared<HardwarePullerEmu>(...);  // ③ 仿真启动
  hal_holder.scheduler.registerKernel(0, "simple_kernel");
  hal_holder.scheduler.setLaunchCallback(...);              // ③ 调度
  hal_holder.puller->start();                               // ③ Puller FSM
  auto device = std::make_shared<GpgpuDevice>(&hal_holder.hal);  // ② 驱动初始化
  VFS::instance().register_device(dev);                     // ① 注册到 /dev/gpgpu0
}
```

→ **`plugin.cpp` 是 UsrLinuxEmu 端的"驱动插件入口"**，它实例化：HAL 桥 + ③ 仿真 + ② 驱动 + ① VFS 注册。它**不实例化**任何"用户态驱动"组件。

---

## 4. IOCTL 接口边界（核心问题 2）

### 4.1 完整 IOCTL 命令集（暴露给用户态）

来源：`plugins/gpu_driver/shared/gpu_ioctl.h:40-247`：

| 范围 | 命名 | 类别 | 行为 |
|------|------|------|------|
| 0x01 | `GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH` | 数据面 | 提交 GPFIFO entry 数组（命令提交主路径） |
| 0x02 | `GPU_IOCTL_REGISTER_MMU_EVENT_CB` | 异步通知 | 注册 MMU page migration 回调 |
| 0x03 | `GPU_IOCTL_REGISTER_FIRMWARE_CB` | 异步通知 | 注册固件回调（CPU task dispatch） |
| 0x10 | `GPU_IOCTL_ALLOC_BO` | 控制面 | 分配 GPU buffer object（GEM/TTM） |
| 0x11 | `GPU_IOCTL_FREE_BO` | 控制面 | 释放 BO |
| 0x12 | `GPU_IOCTL_MAP_BO` | 控制面 | 映射 BO 到 GPU VA |
| 0x13 | `GPU_IOCTL_WAIT_FENCE` | 同步 | 等待 fence 信号 |
| 0x30 | `GPU_IOCTL_CREATE_VA_SPACE` | Phase 2 | 创建 GPU VA Space |
| 0x31 | `GPU_IOCTL_DESTROY_VA_SPACE` | Phase 2 | 销毁 VA Space |
| 0x32 | `GPU_IOCTL_REGISTER_GPU` | Phase 2 | GPU 注册到 VA Space |
| 0x40 | `GPU_IOCTL_CREATE_QUEUE` | UMQ (ADR-024) | 创建 Queue + 分配 Ring Buffer |
| 0x41 | `GPU_IOCTL_DESTROY_QUEUE` | UMQ | 销毁 Queue |
| 0x42 | `GPU_IOCTL_MAP_QUEUE_RING` | UMQ | mmap Ring Buffer 到用户态 |
| 0x43 | `GPU_IOCTL_QUERY_QUEUE` | UMQ | 查询 Queue 状态 |
| (Post-0x43) | `GPU_IOCTL_GET_DEVICE_INFO` | 协商 | 设备能力查询 |

### 4.2 关键洞察：Doorbell mmap 快速路径

ADR-024 §决策 1 在 0x40-0x43 新增 Queue 管理 ioctl，并配合 mmap 提供**用户态直写 Ring Buffer + Doorbell** 的快速路径，**完全绕过内核 syscall**。

来源：ADR-024 §1.2 真实硬件对比表，`adr-024:32-39`：

| 特性 | AMD/NVIDIA 真实 GPU | UsrLinuxEmu 当前设计 |
|------|---------------------|---------------------|
| 命令写入 | **用户态直接写 Ring Buffer (VRAM)** | ioctl 系统调用 |
| Doorbell 触发 | **MMIO BAR 直写（用户态 mmap）** | HAL 函数调用 |
| Kernel 角色 | **仅队列创建 + 异常处理** | 每次命令提交 |

ADR-024 决策 1 明确把以下能力移到用户态（`adr-024:128-219`）：

1. **Ring Buffer 共享内存** — 用户态直接读写
2. **Doorbell mmap 触发** — 用户态 MMIO 直写
3. **Fence 轮询** — 用户态轮询 GPU 内存中的 fence 值

**关键**：ADR-024 的"用户态"指的是**真实硬件下的用户态驱动**（即 libcuda.so / cuStream / HIP runtime），不是 UsrLinuxEmu 的 drv/ 模拟代码。

---

## 5. 用户态驱动 vs 内核驱动边界（核心问题 3）

### 5.1 核心论断

**UsrLinuxEmu 没有"用户态驱动 vs 内核驱动"对立**。它**本身就是"用户态内核模拟环境"**。

- **模拟的对象** = Linux **内核态**驱动（在用户态运行）
- **"用户态驱动"** 这个概念在 UsrLinuxEmu 中**不直接存在**
- TaskRunner 扮演的角色是 **"驱动接口的 consumer（消费者）"** — 它在 UsrLinuxEmu 仿真场景下是测试工具，在真机部署场景下就是用户态 driver（如 libcuda.so 等价物）

### 5.2 边界对照表

| 边界 | UsrLinuxEmu 仿真场景 | 真实硬件场景 |
|------|----------------------|-------------|
| **TaskRunner / 真实用户态驱动** | 测试工具 / consumer of IGpuDriver | libcuda.so / cuStream / HIP runtime |
| **drv/ 模拟代码** | "在用户态运行的内核态驱动代码" | 真实 KFD 内核模块（直接复制 drv/ 即可） |
| **HAL 桥** | 指向 sim/ | 指向真实硬件寄存器 |
| **sim/** | 仿真行为 | （不存在，删除） |

### 5.3 drv/ 不是"用户态驱动"的证据

来源：ADR-018 决策 3 驱动代码可移植 C++ 子集，`adr-018:90-100`：

> 允许：基础 C++（类、继承、虚函数）、`linux_compat` 链表/容器、Linux 错误码、简单 RAII、`int/u32/u64`
> 禁止：RTTI、`std::vector/map`、C++ 异常、`std::shared_ptr/unique_ptr`、`std::cout`、`std::this_thread`

→ drv/ 代码**不能**用 C++ 标准库，**不能**用线程/mutex，**必须**用 Linux 错误码。**这是内核态驱动的代码风格，不是用户态驱动**。

---

## 6. 硬件相关设置归属（核心问题 4）

### 6.1 3 区分 + HAL 桥的明确划分

| 硬件相关设置 | 归属层 | 理由 | 引用 |
|------------|--------|------|------|
| **MMIO 寄存器布局（PCIe config space, BAR, capability）** | ① | 内核 API，由 `pcie_emu` 实现 | `stage-1-kernel-emu.md:55-66` |
| **IOMMU group + ioasid 数据结构** | ① | Linux 内核 iommu_domain / iommu_group | `stage-1-kernel-emu.md:91-95` |
| **DRM ioctl 派发表（drm_ioctl_desc）** | ①+② | drm_ioctl_desc 是内核 API（①）+ driver 定义（②） | `adr-019:96-112` |
| **BuddyAllocator 算法** | `libgpu_core/`（③） | 纯地址运算，零依赖，可移植 | `adr-020:64-92` |
| **Hardware Puller FSM（IDLE→FETCH→DECODE→...→COMPLETE）** | ③ | 仿真硬件行为，**不移植**到内核 | `adr-021:54-93` |
| **Doorbell 寄存器仿真** | ③ | 模拟硬件寄存器 | `adr-021:186-202` |
| **Compute Unit 4 个 kernel template** | ③ | 仿真 kernel 执行 | `adr-022:39-46` |
| **GPFIFO entry 格式** | ②+shared | ② 解析 + shared/ABI 契约 | `adr-017:135-143` |
| **Ring Buffer 内存** | ② 经 UMQ 路径 | ② 创建并 mmap 给用户态 | `adr-024:194-219` |
| **11 个 HAL ops** | HAL 桥 | ② 调 HAL，HAL 调 ③ | `adr-023:48-62` |
| **错误码语义（-EINVAL、-ENOMEM、-EREMOTEIO）** | ①+② | Linux 错误码约定 | `adr-023:113-118` |
| **fence/semaphore 行为** | ③ | 模拟硬件 semaphore 寄存器 | `adr-021:127-148` |

### 6.2 "硬件相关设置应该在用户态驱动完成" 论断评估

**结论：此论断约 40% 正确，60% 偏差。**

#### 正确的部分（约 40%）

**真实硬件下的用户态驱动**（NVIDIA cuStream / AMD ROCm runtime）确实负责：

| 设置 | 用户态驱动责任 | 来源 |
|------|--------------|------|
| Ring Buffer 内存管理 | ✅ 用户态分配 + mmap | ADR-024 §2.1 AMD UMQ, `adr-024:54-77` |
| Doorbell 触发 | ✅ 用户态 mmap MMIO 直写 | ADR-024 §2.2 NVIDIA GPFIFO, `adr-024:82-97` |
| GPFIFO entry 编码 | ✅ 用户态填写 | ADR-021 §决策 2, `adr-021:99-118` |
| User-mode fence 等待 | ✅ 用户态轮询 GPU 内存 | ADR-024 决策 1 提交流程, `adr-024:194-219` |

#### 偏差的部分（约 60%）

**真实硬件下，硬件相关设置还有大量是"内核驱动"负责**：

| 设置 | 内核驱动责任 | 来源 |
|------|------------|------|
| PCIe config space / BAR 初始化 | ✅ 内核模块初始化 | `stage-1-kernel-emu.md:55-66` |
| MSI-X 中断注册 | ✅ 内核 request_irq | `stage-1-kernel-emu.md:55-66` |
| IOMMU group 建立 | ✅ 内核 iommu_domain | `stage-1-kernel-emu.md:91-95` |
| DRM 设备注册（drm_device） | ✅ 内核 drm_dev_register | `adr-019:96-112` |
| GEM object 生命周期 | ✅ 内核 drm_gem_* | `adr-019:115-126` |
| TTM BO 管理 | ✅ 内核 ttm_bo_* | `adr-019:127-141` |
| GPU reset / power management | ✅ 内核 | `blueprint.md:23-37` |
| 固件加载（GPU firmware） | ✅ 内核 request_firmware | `adr-024 §2.3 Intel Gen12+` |
| ATS 协议响应 | ✅ 内核 + IOMMU 硬件 | `stage-1-kernel-emu.md:85-115` |

### 6.3 UsrLinuxEmu 的核心设计哲学（反驳"硬件设置都在用户态驱动"）

来源：ADR-001 §理由 5 条 + ADR-018 §决策 1 + ADR-036 §Context：

> **"开发在用户态，部署到内核。仿真即验证，验证即迁移。"** — `docs/PRD.md:21`

UsrLinuxEmu 的设计哲学是：

- **drv/ 写的是"内核态驱动代码"**（在用户态仿真环境运行）
- **所有硬件访问通过 HAL**（`adr-018:84` 明确："`drv/` 不直接调用 `sim/` — 所有硬件访问通过 `hal/` 接口"）
- **目标**：`drv/` 代码将来**零修改**复制到 `drivers/gpu/xxx/` 编译进真实内核

**因此**：在 UsrLinuxEmu 设计中，**"硬件相关设置"既不在"用户态驱动"也不在"内核驱动"中**——而是在 **drv/（按内核态驱动代码风格 + linux_compat）+ HAL 抽象 + sim/ 仿真** 三者之间分布。**HAL 桥是这一分布的核心机制**。

---

## 7. TaskRunner 角色（核心问题 5）

### 7.1 TaskRunner 的 5 个角色

TaskRunner 同时承担 **5 个角色** — **测试驱动 / 调度器原型 / CLI 工具 / 跨仓架构验证 / 可移植性 PoC**。**不是**"用户态驱动"，**不是**"生产 GPU 驱动"。

#### 角色 1：测试驱动（H-2.5 引入）

- `IGpuDriver` 抽象 28 个方法
- 3 个实现：
  - `GpuDriverClient`（真 ioctl）— 28 真实方法
  - `CudaStub`（in-memory mock）— 28 mock 方法
  - `MockGpuDriver`（headless）— 28 测试方法
- DI 注入：`CudaScheduler` 接受 `IGpuDriver*`（默认 nullptr → 自动 new `CudaStub`）

引用：ADR-032 §核心架构图，`adr-032:32-52` + TADR-005

#### 角色 2：调度器原型（CudaScheduler）

- `CudaScheduler` 持有 `IGpuDriver*` via DI
- 集成 `CmdBuffer` / `fence` / `event` 抽象
- 单测 `test_cuda_scheduler` 8/8 PASS

引用：`docs/architecture/current.md:24-27` + `docs/adr/README.md`

#### 角色 3：CLI 工具

- `cmd_cuda.cpp` 提供 6 个 subcommand：
  - `cuda_alloc` / `cuda_free`（内存）
  - `cuda_memcpy`（H2D / D2D / D2H）
  - `cuda_launch`（kernel dispatch）
  - `cuda_wait`（fence 等待）
  - `cuda_va_space`（create / destroy，H-3 新增）
  - `cuda_queue`（create / destroy，H-3 新增）
- 用途：手动验证 GPU 插件功能 + 集成测试

引用：ADR-033 §Migration 段：`241f3ed..8625b82, 9 commits, 2026-06-23`

#### 角色 4：跨仓架构验证（IGpuDriver 28 方法的"接口消费者"）

- TaskRunner 是 IGpuDriver 抽象的**真实 consumer**
- 通过 12 doctest cases（`test_gpu_phase2.cpp`）+ 11 cases（`test_gpu_architecture.cpp`）+ 8 cases（`test_cuda_scheduler.cpp`）持续验证 IGpuDriver 契约

引用：ADR-032 §核心架构 + ADR-033 §测试覆盖

#### 角色 5：可移植性 PoC（验证"驱动代码风格"假设）

- TaskRunner 不直接写"用户态驱动"，但**验证了真实生产用户态驱动需要什么**：
  - GPU handle 生命周期（VA Space / Queue handle）
  - 同步原语（fence_id）
  - 命令编码（GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH）
  - 错误处理（-EINVAL/-ENOMEM 等）
- 真实生产用户态驱动（libcuda.so）会需要类似的接口设计

引用：TADR-001 §实施路径备注 + TADR-006 §Consumer-Lens

### 7.2 TADR-001~004 的设计意图 vs 实际偏离

| TADR | 标题 | 主题 | 实际偏离 |
|------|------|------|---------|
| TADR-001 | CUDA/Vulkan Runtime 统一调度器（B 方案） | 选择 UnifiedScheduler 中央调度 | **实际改为 IGpuDriver 抽象 + DI**，未实现 UnifiedScheduler 类 |
| TADR-002 | 分层设计（C 方案） | TaskRunner 20+ 命令 + UsrLinuxEmu 5 种基础命令 + CommandTranslator | **实际未实现 CommandTranslator**，命令通过 ioctl 编号直接扩展 |
| TADR-003 | Barrier/Event 同步统一内部表示 | SyncSource + SyncManager 抽象 | **实际简化为 fence_id 机制**（H-3 实施） |
| TADR-004 | Runtime Stub 层独立追踪 | Stub 维护 VA Space 句柄、生命周期 | **实际一致** — CudaStub 维护 `next_va_space_handle_` atomic + `va_space_map_` |

→ **TaskRunner v0.1 提案与实际实施的偏差在 TADR-001/002/003 上很显著**（unified scheduler / translator / SyncManager 都没实现），原因是 H-2.5 引入的 IGpuDriver 抽象替代了这些角色。

---

## 8. AMD / NVIDIA 借鉴（核心问题 6）

### 8.1 借鉴关系总表

UsrLinuxEmu 大量借鉴 AMD ROCm + NVIDIA GPFIFO + Intel GuC，但有自己的简化路径（operator-level emulation，不做 ISA 解释）。

| 来源 | UsrLinuxEmu 借鉴 | 引用 |
|------|-----------------|------|
| **AMD ROCm 控制面/数据面分离** | System C IOCTL 分类（控制面：ALLOC/BO/VA Space；数据面：SUBMIT_BATCH；同步：WAIT_FENCE） | ADR-015 §Q1, `adr-015:196-211` |
| **AMD ROCm Memory domains（VRAM/GTT/CPU）** | ALLOC_BO 增加 memory domain 参数 | ADR-015 §Librarian AMD 视角, `adr-015:294-302` |
| **AMD ROCm Fence 模型 (ctx_id, ip_type, ring, seq_no)** | 未来扩展 fence 结构 | ADR-015 §Librarian AMD 视角 |
| **AMD CDNA2 ACE (Asynchronous Compute Engine)** | Global Scheduler 概念 | ADR-021 决策 5, `adr-021:160-184` |
| **AMD AQL Queue（User Queue + Doorbell + AQL Packets）** | GPFIFO/Queue 抽象灵感来源 | ADR-017 §2 AMD AQL Queue, `adr-017:50-64` |
| **AMD UMQ（User Mode Queue, GFX11+/CDNA3）** | UMQ 提交路径决策 1（共享 Ring Buffer + mmap Doorbell） | ADR-024 §2.1, `adr-024:54-77` |
| **NVIDIA Channel/GPFIFO 模型** | GpuQueue + GpuVaSpace + semaphore_va/value/release 字段 | ADR-017 §2.1, `adr-017:30-48` + ADR-021 决策 3, `adr-021:127-148` |
| **NVIDIA PBDMA / Pushbuffer / HyperQ** | Hardware Puller FSM + 决策 5 Global Scheduler | ADR-021 §背景, `adr-021:36-43` |
| **NVIDIA userd (user doorbell) mapping** | Doorbell mmap 路径 | ADR-024 §2.2, `adr-024:80-98` |
| **Intel Gen12+ LRCA (Logical Ring Context Address) + GuC** | 提交路径分层的概念模型 | ADR-024 §2.3, `adr-024:101-117` |
| **Linux 内核 `struct xxx_ops` 模式** | `struct gpu_hal_ops` + `drm_driver` | ADR-023 决策 2, `adr-023:74-82` + ADR-019 决策 1-2, `adr-019:65-112` |
| **Linux 内核 DRM/GEM/TTM 范式** | drm_ioctl_desc[] + drm_gem_object + ttm_bo_driver 骨架 | ADR-019 §背景, `adr-019:17-63` |
| **Linux 内核 mmu_notifier** | MMU 事件模型（`libgpu_core/gpu_mmu_events.h`） | ADR-020 决策 1, `adr-020:64-92` |

### 8.2 ROCm AQL Queue vs UsrLinuxEmu GPFIFO Queue

| 维度 | ROCm AQL Queue | UsrLinuxEmu GPFIFO Queue |
|------|---------------|--------------------------|
| **层级** | 用户态语言层（command packet format） | 硬件 ring buffer 协议 |
| **格式** | 64-byte AQL packets（architected queuing language） | `gpu_gpfifo_entry` 结构（method + payload） |
| **关系** | AQL packet 可通过 GPFIFO entry 承载 | 模拟 GPFIFO 协议（不直接支持 AQL packet format） |
| **门铃机制** | Doorbell MMIO 写 | `*(volatile u32*)doorbell_ptr = queue_id`（ADR-024 决策 1） |
| **使用方** | ROCm runtime（hsakmt / libhsa-runtime） | UsrLinuxEmu 的 HardwarePullerEmu（FSM 消费者） |

来源：ADR-017 §2 AMD AQL Queue vs NVIDIA Channel/GPFIFO, `adr-017:28-63`。

→ **不同层级的关系**：AQL 是语言层，GPFIFO 是协议层。UsrLinuxEmu 直接模拟 GPFIFO 协议（更接近硬件），不模拟 AQL packet format。

### 8.3 NVIDIA cuStream / hipStream 借鉴

**答：没有直接借鉴 cuStream/hipStream 内部设计**，但有概念对等：

| NVIDIA / AMD | UsrLinuxEmu 对应物 |
|--------------|-------------------|
| `cuStream` / `hipStream` | （无直接对等）— TaskRunner 的 `CudaScheduler` 角色更接近，但语义不同 |
| `cudaStream_t` | TaskRunner `LaunchParams.stream_id` |
| `cuStreamAddCallback` | H-3 `wait_fence` ioctl |
| `cuStreamSynchronize` | H-3 `wait_fence` ioctl（blocking） |
| `cudaEvent_t` | TADR-003 规划但**未实现**（实际使用 `fence_id` 简化） |
| `cuStreamWaitEvent` | TADR-003 规划但**未实现** |

来源：TADR-003 §实施路径备注, `tadr-003:64-72`：

> 当前代码中 `include/sync_primitives.hpp` 实现了基础 `Barrier` 类型，但 `SyncSource` / `SyncManager` 完整设计未实施。H-3 引入的 `fence_id` 机制是简化的同步路径。

→ **TaskRunner 当前不是"用户态 CUDA Runtime"，它只是验证接口契约的消费者**。

---

## 9. 路线图方向（核心问题 7）

### 9.1 路线图结构

来源：`docs/roadmap/README.md`：

| 阶段 | 状态 | 目标 | 关键文件 |
|------|------|------|---------|
| **Stage 0** | ✅ 已达成 | MVP，单一 GPGPU 设备可验证 | `stage-0-mvp.md` |
| **Stage 1** | 🔄 计划中 | Linux 内核环境模拟（DRM + UVM + IOMMU + ATS + PCIe） | `stage-1-kernel-emu.md` |
| **Stage 2** | 📋 规划中 | 多设备插件化（网络 + 存储） | `stage-2-multi-device.md` |
| **Stage 3** | 📋 规划中 | v1.0 稳定（CI 全平台、文档、性能） | `stage-3-v1.0.md` |
| **蓝图** | 📋 愿景 | 3 区分成熟形态，可移植驱动可在真实 Linux 内核中编译运行 | `blueprint.md` |

### 9.2 Stage 1 详细（5 子阶段）

来源：`docs/roadmap/stage-1-kernel-emu.md:42-48`：

| 子阶段 | 主题 | 涉及层 | 关键交付 |
|--------|------|--------|----------|
| 1.0 | PCIe 设备模拟 | ① | config space + BAR + MSI-X |
| 1.1 | IOMMU + ATS | ① ② ③ | DMA remapping + ATS 协议 |
| 1.2 | DRM 子集 | ① ② | GpgpuDevice 重构为 drm_device 风格 |
| 1.3 | UVM/HMM | ① ② ③ | mmu_notifier + migrate + fault |
| 1.4 | **集成验证** | 全部 | **编译真实 KFD + 5 个 ioctl** |

**Stage 1.4 集成验证的关键验收**（`stage-1-kernel-emu.md:200-232`）：

1. 取 KFD 源码（Linux 6.6/6.12 LTS）
2. 零修改移植到 `plugins/gpu_driver/drv/kfd/`
3. 编译通过（errors = 0）
4. **跑通 5 个核心 KFD ioctl**（`AMDKFD_IOC_GET_PROCESS_APERTURE` / `CREATE_QUEUE` / `UPDATE_QUEUE` / `MAP_MEMORY` / `UNMAP_MEMORY`）

### 9.3 TaskRunner 在各阶段的角色

| 阶段 | TaskRunner 角色 | 具体工作 |
|------|----------------|----------|
| **Stage 0（已完成）** | 测试驱动 + 调度器原型 + CLI 工具 | 5 subcommand（cuda_alloc/memcpy/launch/wait）+ 28 IGpuDriver 方法 + 12 Phase 2 测试 |
| **Stage 1.0-1.3** | （无新增角色） | 跟随 IOCTL 编号扩展验证新 ABI；可能新增测试用例覆盖 PCIe / IOMMU / UVM 行为 |
| **Stage 1.4** | **集成验证 consumer** | 测试 KFD 5 个 ioctl 跑通（TaskRunner 模拟"真实 CUDA 应用调用 KFD"） |
| **Stage 2** | （不参与） | Stage 2 是网络 + 存储设备，TaskRunner 专注 GPU，**不参与** |
| **Stage 3** | 测试基线维护 | 跟随 v1.0 稳定化（CI 全平台、错误处理、文档） |
| **蓝图** | **不演化为真实生产用户态驱动** | TaskRunner 保持"测试 / 调度 / CLI"定位 |

来源：`stage-1-kernel-emu.md:163` 明确 UMQ **不在 1.3 UVM 范围**（UMQ 由 ADR-024 完整覆盖）。

### 9.4 TaskRunner 未来 6-12 月可能的演化方向

#### 大概率（基础）

1. **H-3.5 短期 follow-up**（TADR-006 §Follow-up + ADR-033 §Consequences）：
   - 4 个 CudaStub-based guard verification tests（关闭 T6-T9 mock-behavior deviation）
   - 修复 `cmd_buffer_v2.cpp` `--help` 不更新问题

2. **H-7 上游问题解决**（ADR-034，⏸️ Deferred）：
   - stream_id u32 类型不匹配
   - ioctl 绕过 GpuQueueEmu
   - attached_queues 弱校验

3. **Phase 3 Multi-GPU / P2P**（roadmap/phase-3.md，⏸️ Deferred）：
   - H-7 解决后启动
   - TaskRunner 可能新增 multi-GPU 测试用例

#### 中等可能

4. **Stage 1.4 集成验证后**：TaskRunner 可能演化为"KFD 集成测试的 1 级 consumer"
5. **Stage 3 v1.0**：TaskRunner CI 全平台绿、错误注入测试、性能回归基线

#### 小概率（明确不演化方向）

6. **不演化为真实生产用户态驱动**（如 NVIDIA cuStream）：
   - 蓝图 §非可达愿景 第 5 条：仅移植 KFD 子集
   - ADR-035 §1.2 governance：跳过 Stage 1.4 集成就无法证明可移植性
   - TaskRunner 的设计目标是验证 `drv/` 路径，**不是**成为用户态驱动的事实标准

7. **不演化为多 API 完整 Runtime**：
   - TADR-001 B 方案（UnifiedScheduler）**未实施**
   - Vulkan Compute stub 仍为 Phase 3+ 规划
   - TADR-003 SyncManager / SyncSource **未实施**

---

## 10. 论断最终评估

### 10.1 总判定

| 维度 | 判定 | 理由 |
|------|------|------|
| **真实硬件下用户态驱动负责的能力** | ✅ 正确 | Ring Buffer 写入、Doorbell mmap 触发、GPFIFO entry 编码、user-mode fence 轮询（ADR-024 + ADR-021） |
| **所有硬件相关设置都应集中在用户态驱动** | ❌ 错误 | 大量硬件设置必须在内核驱动：PCIe config 初始化、MSI-X 注册、IOMMU group、DRM 设备注册、GEM object、TTM BO、GPU reset/power、固件加载、ATS 协议（stage-1-kernel-emu.md） |
| **UsrLinuxEmu 中"硬件相关设置"应该集中在 drv/** | ❌ 错误 | 严格按 3 区分原则：drv/（按内核态代码风格 + linux_compat）+ HAL 桥（11 个 fn-ptr）+ sim/（仿真行为）。**HAL 是分布核心**（ADR-036 + ADR-018） |
| **TaskRunner 应该承担更多用户态驱动责任** | ❌ 错误 | TaskRunner 定位是"测试 / 调度 / CLI"，未来蓝图不演化为生产用户态驱动（blueprint.md + ADR-001 + TADR-001） |
| **AMD ROCm AQL Queue = UsrLinuxEmu GPFIFO Queue** | ❌ 不准确 | AQL 是用户态语言层（command packet format），GPFIFO 是硬件 ring buffer 协议。AQL packet 可通过 GPFIFO entry 承载，但 UsrLinuxEmu 不直接实现 AQL（ADR-017） |

### 10.2 正确的"硬件相关设置归属"模型

```
                    真实硬件场景
                    ┌─────────────────────────────────┐
                    │  用户态驱动 (libcuda.so)         │
                    │  ─────────────────────────────  │
                    │  ✓ Ring Buffer 内存管理          │
                    │  ✓ Doorbell mmap 触发            │
                    │  ✓ GPFIFO entry 编码             │
                    │  ✓ User-mode fence 轮询          │
                    └─────────────┬───────────────────┘
                                  │ ioctl
                    ┌─────────────▼───────────────────┐
                    │  内核驱动 (KFD)                  │
                    │  ─────────────────────────────  │
                    │  ✓ PCIe config 初始化           │
                    │  ✓ MSI-X 注册                   │
                    │  ✓ IOMMU group / DMA remap      │
                    │  ✓ DRM 设备注册                  │
                    │  ✓ GEM object 生命周期           │
                    │  ✓ TTM BO 管理                  │
                    │  ✓ GPU reset / power            │
                    │  ✓ 固件加载                      │
                    │  ✓ ATS 协议响应                 │
                    └─────────────┬───────────────────┘
                                  │ MMIO
                    ┌─────────────▼───────────────────┐
                    │  真实硬件（GPU）                  │
                    └─────────────────────────────────┘

                    UsrLinuxEmu 仿真场景
                    ┌─────────────────────────────────┐
                    │  TaskRunner (测试 / 调度 / CLI)  │
                    └─────────────┬───────────────────┘
                                  │ IGpuDriver 28 方法
                    ┌─────────────▼───────────────────┐
                    │  ② drv/ (内核态代码风格)         │
                    │  ─────────────────────────────  │
                    │  ✓ GpgpuDevice                  │
                    │  ✓ DRM 风格 ioctl 派发            │
                    │  ✓ BO / VA Space / Queue 抽象    │
                    │  ✓ 全部用 linux_compat + HAL     │
                    └─────────────┬───────────────────┘
                                  │ HAL ops (11 fn-ptr)
                    ┌─────────────▼───────────────────┐
                    │  HAL 桥                          │
                    │  ─────────────────────────────  │
                    │  register_read/write             │
                    │  mem_read/write                  │
                    │  doorbell_ring                   │
                    │  interrupt_raise                 │
                    │  fence_create/read               │
                    └─────────────┬───────────────────┘
                                  │ 反向 ctx 调用
                    ┌─────────────▼───────────────────┐
                    │  ③ sim/ (仿真)                  │
                    │  ─────────────────────────────  │
                    │  ✓ HardwarePullerEmu FSM        │
                    │  ✓ GlobalScheduler              │
                    │  ✓ DoorbellEmu                  │
                    │  ✓ BuddyAllocator               │
                    │  ✓ ComputeUnitEmu (4 templates) │
                    └─────────────────────────────────┘
```

**关键洞察**：在 UsrLinuxEmu 中，"硬件相关设置"分布在 ②+③+HAL 三者之间，**不是"用户态驱动 vs 内核驱动"的简单二分**。HAL 桥是这种分布的核心机制。

---

## 引用清单

### UsrLinuxEmu ADR（核心决策）

| ADR | 主题 | 关键引用 |
|-----|------|---------|
| ADR-001 | 用户态模拟基础决策 | `docs/00_adr/adr-001-user-mode-emulation.md:18,21-26,32` |
| ADR-015 | System C IOCTL 统一 | `docs/00_adr/adr-015-gpu-ioctl-unification.md:196-211,294-302` |
| ADR-017 | NVIDIA Channel/AMD AQL 借鉴 | `docs/00_adr/adr-017-gpfifo-queue-abstraction.md:28-63,135-143` |
| ADR-018 | drv/hal/sim/shared 物理分离 | `docs/00_adr/adr-018-driver-sim-separation.md:72-100` |
| ADR-019 | DRM/GEM/TTM 范式对齐 | `docs/00_adr/adr-019-drm-gem-ttm-alignment.md:65-141` |
| ADR-020 | libgpu_core 纯 C 提取 | `docs/00_adr/adr-020-libgpu-core-extraction.md:64-92` |
| ADR-021 | Hardware Puller FSM | `docs/00_adr/adr-021-hardware-puller.md:36-43,54-93,99-184` |
| ADR-022 | operator-level emulation | `docs/00_adr/adr-022-gpu-compute-unit-emulation.md:39-46` |
| ADR-023 | HAL 11 ops | `docs/00_adr/adr-023-hal-interface.md:48-82,113-118` |
| ADR-024 | UMQ 用户态队列提交 | `docs/00_adr/adr-024-user-mode-queue-submission.md:32-219` |
| ADR-027 | spec-driven 增量扩展 | `docs/00_adr/adr-027-linux-compat-strategy.md` |
| ADR-032 | IGpuDriver 28 方法抽象 | `docs/00_adr/adr-032-h2-5-igpu-driver-abstraction.md:32-52` |
| ADR-033 | Phase 2 5 方法 | `docs/00_adr/adr-033-h3-phase2-lifecycle.md`（241f3ed..8625b82, 9 commits, 2026-06-23） |
| ADR-035 | 跨仓 governance | `docs/00_adr/adr-035-governance-policy.md` |
| ADR-036 | 3 区分架构原则（元决策） | `docs/00_adr/adr-036-three-way-separation.md:17,35-67` |

### 路线图文档

| 文档 | 路径 | 关键引用 |
|------|------|---------|
| 路线图总览 | `docs/roadmap/README.md:3` | 项目目标 |
| Stage 0 MVP | `docs/roadmap/stage-0-mvp.md` | ✅ 已达成 |
| Stage 1（5 子阶段） | `docs/roadmap/stage-1-kernel-emu.md:42-48,55-115,163,200-232` | 关键 1.4 集成验证段 |
| Stage 2 多设备 | `docs/roadmap/stage-2-multi-device.md` | 网络 + 存储 |
| Stage 3 v1.0 | `docs/roadmap/stage-3-v1.0.md` | CI 全平台 |
| 蓝图终态 | `docs/roadmap/blueprint.md:23-37` | 可移植驱动愿景 |

### UsrLinuxEmu 项目文档

| 文档 | 路径 | 关键引用 |
|------|------|---------|
| PRD | `docs/PRD.md:17,21,30-35,96-101,154-165` | 4 类目标用户 + 10 条可移植性约束 |
| 3-way 分离治理计划 | `.omo/plans/docs-3way-separation-and-roadmap.md` | Wave 1-4 + F1-F4 review |

### GPU 驱动代码（UsrLinuxEmu）

| 文件 | 路径 | 关键引用 |
|------|------|---------|
| 插件入口 | `plugins/gpu_driver/plugin.cpp:30-99` | HAL 注入 + ③ 仿真 + ② 驱动 + ① VFS 注册 |
| GpgpuDevice 头 | `plugins/gpu_driver/drv/gpgpu_device.h` | 13 个 ioctl handler + VA Space/Queue 管理 |
| canonical 接口 | `plugins/gpu_driver/shared/gpu_ioctl.h:40-247` | 15 个 GPU_IOCTL_* 编号 0x01-0x43 |

### TaskRunner 端 TADR

| TADR | 主题 | 关键引用 |
|------|------|---------|
| TADR-001 | UnifiedScheduler B 方案 | `docs/adr/tadr-001-cuda-vulkan-runtime-unified-scheduler.md`（**未实施**） |
| TADR-002 | 分层设计 C 方案 | `docs/adr/tadr-002-cuda-vulkan-runtime-layered-design.md`（**未实施**） |
| TADR-003 | SyncManager 抽象 | `docs/adr/tadr-003-cuda-vulkan-runtime-sync-unified-internal.md:64-72`（**未实施**） |
| TADR-004 | Runtime Stub 独立追踪 | `docs/adr/tadr-004-cuda-vulkan-runtime-stub-tracker.md`（**实际一致**） |
| TADR-005 | IGpuDriver consumer-lens | `docs/adr/tadr-005-h2-5-igpu-driver-consumer-lens.md` |
| TADR-006 | Consumer-Lens | `docs/adr/tadr-006-consumer-lens.md` |
| TADR-101~106 | test-fixture 新编号 | `docs/test-fixture/adr/tadr-101~106` |
| TADR-107 | shared boundary | `docs/shared/adr/tadr-107-shared-infrastructure-boundary.md` |
| TADR-201~205 | umd-evolution 编号 | `docs/umd-evolution/adr/tadr-201~205` |
| TADR-301~303 | shared contracts | `docs/shared/adr/tadr-301-igpu-driver-contract.md`, `tadr-302-sync-primitives.md`, `tadr-303-error-handling.md` |

### TaskRunner 端架构文档

| 文档 | 路径 | 关键引用 |
|------|------|---------|
| 5 层架构图 | `docs/test-fixture/architecture/current.md:24-27` | App → Stub → Scheduler → IGpuDriver → Backend |
| 分层视图 | `docs/test-fixture/architecture/layers.md` | 5 层结构 |
| 数据流 | `docs/test-fixture/architecture/data-flow.md` | CUDA kernel launch 路径 |

---

## Cross-References

Related research documents in this repository:

- [TaskRunner 自身定位与能力边界](../../test-fixture/research/taskrunner-positioning-2026-06-24.md) `[TEST-FIXTURE SCOPE]`
- [NVIDIA CUDA 用户态驱动架构](../../umd-evolution/research/external-nvidia-cuda-umd-2026-06-24.md) `[UMD-EVOLUTION SCOPE]`
- [AMD ROCm 用户态驱动架构](../../umd-evolution/research/external-amd-rocm-umd-2026-06-24.md) `[UMD-EVOLUTION SCOPE]`

Upstream documents:

- [Umbrella vision-source.md](../../umd-evolution/vision-source.md)
- [Umbrella vision.md](../../umd-evolution/vision.md)
- [Umbrella gap-analysis.md](../../umd-evolution/gap-analysis.md)

Cross-repo references (UsrLinuxEmu docs/00_adr/):

- [ADR-036: 3-way architectural separation](../../../../docs/00_adr/adr-036-three-way-separation.md)
- [ADR-001: User-mode emulation strategy](../../../../docs/00_adr/adr-001-user-mode-emulation.md)
- [ADR-024: User-mode queue submission (UMQ)](../../../../docs/00_adr/adr-024-user-mode-queue-submission.md)
- [ADR-023: HAL interface](../../../../docs/00_adr/adr-023-hal-interface.md)
- [ADR-021: Hardware puller FSM](../../../../docs/00_adr/adr-021-hardware-puller.md)
- [Stage 1 roadmap: Kernel environment emulation](../../../../docs/roadmap/stage-1-kernel-emu.md)
