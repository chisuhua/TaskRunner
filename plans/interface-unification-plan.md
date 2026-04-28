# Task Plan: TaskRunner-UsrLinuxEmu 接口规范统一

## 目标

统一 TaskRunner 和 UsrLinuxEmu 之间的接口规范，解决当前文档与代码、两个项目之间的命名不一致问题，建立清晰的接口规范和协作流程。

## 关键问题汇总

### 🔴 严重问题（需 P0 修复）

| # | 问题 | 影响 |
|---|------|------|
| K1 | **TaskRunner 调用 `CUDA_IOCTL_*` (magic='C')，但 gpu_driver.cpp 处理 `GPGPU_*` (magic='g')** | **两者完全不兼容！ioctl 请求根本无法到达处理函数！** |
| K2 | **UsrLinuxEmu 内部存在三套 ioctl 体系** (cuda_ioctl.h, ioctl_gpgpu.h, shared/gpu_ioctl.h) | 内部混乱，无法确定事实标准 |
| K3 | **GPGPU_SUBMIT_PACKET 是空实现** (gpu_driver.cpp:68-71 仅 break) | 批量提交完全不可用 |
| K4 | **TaskRunner DOC-01 描述 `GPGPU_*`，代码用 `CUDA_IOCTL_*`** | 文档与代码双重脱节 |

### 🟡 中等问题（需 P1 修复）

| # | 问题 | 影响 |
|---|------|------|
| K5 | `architecture_design.md` 定义 `GPU_*` (magic `'G'`)，但实际代码用 `GPGPU_*` 或 `CUDA_IOCTL_*` | 文档严重过时 |
| K6 | **GET_DEVICE_INFO (0x00)** 在 `cuda_ioctl.h` 中缺失，但在 `ioctl_gpgpu.h` 中存在 (cmd=0) | 功能不完整 |
| K7 | `cuda_compat_ioctl.cpp` 转译层存在但 Stub 模式，**未实际调用 gpu_driver** | 转译层是空壳 |
| K8 | TaskRunner 缺失 CommandTranslator 实现 | 无法完成命令翻译 |

---

## 现状分析（更新）

### 核心问题：ioctl 根本无法对接！

```
TaskRunner cmd_cuda.cpp
         │
         │ 调用 CUDA_IOCTL_* (magic='C')
         ▼
cuda_ioctl.h (定义)
         │
         │ ⚠️ 但 gpu_driver.cpp 用 switch(request) 匹配的是...
         ▼
gpu_driver.cpp switch
         │
         │ case GPGPU_* (magic='g') ← 不匹配！
         │ case GPGPU_GET_DEVICE_INFO (cmd=0)
         │ case GPGPU_ALLOC_MEM (cmd=1)
         │ case GPGPU_FREE_MEM (cmd=2)
         │ case GPGPU_SUBMIT_PACKET (cmd=5) ← 空实现
         ▼
      default: return -1;  ← 请求被拒绝！
```

### UsrLinuxEmu 侧：三套 ioctl 体系

| 文件 | 前缀 | Magic | 命令范围 | 问题 |
|------|------|-------|---------|------|
| `cuda_ioctl.h` | `CUDA_IOCTL_*` | `'C'` | 0x01-0x31 | ✅ TaskRunner 引用 |
| `ioctl_gpgpu.h` | `GPGPU_*` | `'g'` | 0,1,2,5 | ⚠️ gpu_driver 处理 |
| `shared/gpu_ioctl.h` | `GPU_*` | `'G'` | 0x01-0x20 | 🔄 规划标准 |

### TaskRunner 侧

| 文件 | 内容 | 状态 |
|------|------|------|
| `src/cmd_cuda.cpp` | 使用 `CUDA_IOCTL_*` (via `cuda_ioctl.h`) | ❌ 调不通 |
| `docs/00_UsrLinuxEmu_Interface/DOC-01` | 描述 `GPGPU_*` 命令 | ❌ 与代码双重脱节 |
| `docs/00_UsrLinuxEmu_Interface/DOC-02` | 描述 `GpuCommandPacket` 批量提交 | ⚠️ 未实现 |

### cuda_compat_ioctl.cpp 转译层

存在 `UsrLinuxEmu/src/kernel/device/cuda_compat_ioctl.cpp`，注释说明：
- Phase 1: Stub 模式（简化实现，不实际调用驱动）
- Phase 2: 统一 GPU 接口 (gpu_ioctl.h + 转译层)

**但**: TaskRunner `cmd_cuda.cpp` 直接调用 `cuda_ioctl.h`，**没有**通过这个转译层！

---

## 实施计划

### Phase 0: 接口现状审计 (1天) ⚠️ 紧急

**目标**: 确认三套 ioctl 体系的关系，确定哪个是"事实标准"

**关键发现**:
- TaskRunner 调用 `CUDA_IOCTL_*` (magic='C')
- gpu_driver 处理 `GPGPU_*` (magic='g')
- **两者完全不匹配！当前代码根本无法工作！**

- [ ] 0.1 确认 `cuda_compat_ioctl.cpp` 转译层的作用——TaskRunner 是否应该通过它调用？
- [ ] 0.2 确认 `ioctl_gpgpu.h` 是内部实现还是废弃代码
- [ ] 0.3 确认 UsrLinuxEmu 团队的计划：统一到哪套接口
- [ ] 0.4 输出：`UsrLinuxEmu 接口现状审计报告`，包含三选一的明确建议

**审查点 (CP0)**: 与 UsrLinuxEmu 维护者确认：
1. 当前哪套 ioctl 是"事实标准"？
2. TaskRunner 是否应该通过 `cuda_compat_ioctl.cpp` 转译层调用？
3. 短期是否统一到体系 A/B/C 中的一个？

---

### Phase 1: 统一接口规范定义 (2天)

**目标**: 确定统一的接口命名、结构体、协议

#### Step 1.1: 确定命名规范

**选项分析**:
- `CUDA_IOCTL_*`: TaskRunner 代码已用，UsrLinuxEmu `cuda_ioctl.h` 已定义
- `GPGPU_*`: UsrLinuxEmu `gpu_driver.cpp` 实际调用
- `GPU_*`: 仅在文档中

**推荐**: 保持 `CUDA_IOCTL_*` (magic `'C'`)，因为：
1. TaskRunner 代码已依赖
2. UsrLinuxEmu `cuda_ioctl.h` 已实现
3. 体现这是 CUDA 仿真接口

**废弃**: `GPGPU_*` 和 `GPU_*` 从文档中移除或标记为历史

#### Step 1.2: 统一结构体定义

以 `cuda_ioctl.h` 为准，补充缺失字段：

```cpp
// 统一后的 ioctl 命令 (CUDA_IOCTL_*)
#define CUDA_IOCTL_GET_DEVICE_INFO  _IOR('C', 0x00, struct GpuDeviceInfo)  // 新增 0x00
#define CUDA_IOCTL_MEM_ALLOC       _IOWR('C', 0x01, struct cuda_mem_alloc_request)
#define CUDA_IOCTL_MEM_FREE        _IOWR('C', 0x02, struct cuda_mem_free_request)
#define CUDA_IOCTL_MEMCPY_H2D     _IOWR('C', 0x03, struct cuda_memcpy_h2d_request)
#define CUDA_IOCTL_MEMCPY_D2H     _IOWR('C', 0x04, struct cuda_memcpy_d2h_request)
#define CUDA_IOCTL_LAUNCH_KERNEL  _IOWR('C', 0x10, struct cuda_launch_kernel_request)
#define CUDA_IOCTL_WAIT_FENCE     _IOWR('C', 0x20, struct cuda_wait_fence_request)
#define CUDA_IOCTL_QUERY_FENCE    _IOWR('C', 0x21, struct cuda_query_fence_request)
```

#### Step 1.3: 确认 `gpu_driver.cpp` ioctl 处理

当前 `gpu_driver.cpp` 的 ioctl 处理（lines 43-79）：
```cpp
switch (request) {
    case GPGPU_GET_DEVICE_INFO: ...  // 使用 GPGPU_*，不是 CUDA_IOCTL_*
    case GPGPU_ALLOC_MEM: ...
    case GPGPU_FREE_MEM: ...
    case GPGPU_SUBMIT_PACKET: ...  // 空实现
}
```

**需要修复**: `gpu_driver.cpp` 需要处理 `CUDA_IOCTL_*` 命令，或重新映射

#### Step 1.4: 更新文档

- [ ] 更新 `DOC-01-ioctl-api-spec.md` 使用 `CUDA_IOCTL_*` 命名
- [ ] 更新 `DOC-02-device-command-protocol.md` 说明批量提交为 Phase 2
- [ ] 删除 `DOC-03-command-translator.md` 中不存在的映射（CommandTranslator 待实现）

**审查点**: 与 UsrLinuxEmu 维护者确认接口规范，达成书面一致

---

### Phase 2: 代码实现对齐 (3天)

**目标**: 让 TaskRunner 和 UsrLinuxEmu 代码都符合统一后的接口规范

#### Step 2.1: UsrLinuxEmu 侧修改

- [ ] 修改 `gpu_driver.cpp` 的 ioctl switch 匹配 `CUDA_IOCTL_*`
- [ ] 补充 `CUDA_IOCTL_GET_DEVICE_INFO` (0x00) 处理
- [ ] 清理废弃的 `ioctl_gpgpu.h` 或将其标记为历史

#### Step 2.2: TaskRunner 侧修改

- [ ] 确认 `cmd_cuda.cpp` 的 ioctl 调用与 `cuda_ioctl.h` 定义一致
- [ ] 实现 `CommandTranslator` 类（封装 TaskCommand → CUDA_IOCTL_* 映射）
- [ ] 重构 `cmd_cuda.cpp` 使用 `CommandTranslator`

#### Step 2.3: 编译验证

- [ ] UsrLinuxEmu `gpu_driver.cpp` 编译通过
- [ ] TaskRunner 编译通过（包含 UsrLinuxEmu headers）
- [ ] stub 模式测试通过

**审查点**: 端到端 ioctl 调用测试（TaskRunner → UsrLinuxEmu）

---

### Phase 3: Submodule 和 CI 配置 (1天)

**目标**: 建立两个项目的协作流程

- [ ] TaskRunner 添加 `external/UsrLinuxEmu` submodule（锁定 commit，非跟踪分支）
- [ ] 更新 TaskRunner `CMakeLists.txt` 的 include path
- [ ] 更新 `DOC-04-ci-config.md` 使用锁定 commit 策略
- [ ] 验证 CI 构建

**审查点**: submodule 配置正确，CI 通过

---

### Phase 4: 文档最终化 (1天)

**目标**: 让所有文档准确反映实现状态

- [ ] 统一接口文档移至 UsrLinuxEmu `docs/interface/` 或保持 TaskRunner
- [ ] 更新所有文档引用路径
- [ ] 添加接口变更流程（如何提交接口变更）
- [ ] 补充 CommandTranslator 使用文档

**审查点**: 文档完整性和准确性

---

## 风险清单

| 风险 | 等级 | 缓解措施 |
|------|------|---------|
| UsrLinuxEmu 不同意统一到 `CUDA_IOCTL_*` | 🔴 高 | Phase 0 先审计，Phase 1 审查点充分讨论 |
| 破坏 UsrLinuxEmu 现有构建 | 🔴 高 | 每个修改后立即编译验证 |
| TaskRunner 依赖 `cuda_ioctl.h` 但字段不匹配 | 🟡 中 | Phase 0 确认字段差异 |
| 批量提交需求突然出现 | 🟡 中 | Phase 1 阶段明确为 Phase 2 |
| CommandTranslator 实现复杂度超预期 | 🟡 中 | Phase 2 阶段评审后决定是否简化 |

---

## 审查检查点

| 检查点 | 触发条件 | 参与者 |
|--------|---------|--------|
| CP0: Phase 0 审计完成 | 确认 UsrLinuxEmu 接口现状 | TaskRunner + UsrLinuxEmu 双方 |
| CP1: 接口规范达成一致 | Phase 1 结束 | TaskRunner + UsrLinuxEmu 双方 |
| CP2: 代码修改完成 | Phase 2 结束 | TaskRunner 主导 |
| CP3: 集成测试通过 | Phase 2 编译验证后 | TaskRunner + UsrLinuxEmu 双方 |
| CP4: Submodule + CI 配置完成 | Phase 3 结束 | TaskRunner 主导 |
| CP5: 文档最终评审 | Phase 4 结束 | TaskRunner + UsrLinuxEmu 双方 |

---

## 决策记录

| 日期 | 决策 | 理由 |
|------|------|------|
| 2026-04-27 | **暂停实施计划** | 审计发现当前代码**根本无法工作**：TaskRunner 调用 CUDA_IOCTL_* (magic='C')，但 gpu_driver 处理 GPGPU_* (magic='g') |
| 2026-04-27 | Phase 0 优先级提升 | 必须先确认三套 ioctl 体系的关系，再决定统一到哪套 |
| 2026-04-27 | 批量提交 (SUBMIT_PACKET) 降级为 Phase 2 | 当前未实现，且核心问题未解决前不重要 |
| 2026-04-27 | Submodule 锁定到 commit | 可复现性优先于跟踪最新 |

## ⚠️ 关键风险更新

| 风险 | 等级 | 缓解措施 |
|------|------|---------|
| **当前代码根本无法对接** | 🔴 极高 | Phase 0 审计后决定统一方案 |
| 三套 ioctl 体系需要明确优先级 | 🔴 高 | Phase 0 审查点必须解决 |
| UsrLinuxEmu 内部混乱 | 🟡 中 | 需要与 UsrLinuxEmu 团队澄清 |
| CommandTranslator 实现可能需要重构 | 🟡 中 | 取决于最终选择的 ioctl 体系 |
