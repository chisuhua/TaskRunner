# Progress: TaskRunner-UsrLinuxEmu 接口统一

## 会话目标

审查 TaskRunner 项目修改和新增的代码和文档，检查文档之间、文档和代码是否对齐，并与 UsrLinuxEmu 项目建立统一的接口规范。

## 当前状态

**阶段**: 分析完成，规划阶段

## 已完成工作

### 1. 上下文收集 (并行执行)

- [x] 启动 3 个 explore 代理分析 TaskRunner 项目
- [x] 启动 3 个 explore 代理分析 UsrLinuxEmu 项目
- [x] 启动 1 个 oracle 代理获取架构建议
- [x] 直接读取关键文件：
  - `cuda_ioctl.h` (UsrLinuxEmu)
  - `ioctl_gpgpu.h` (UsrLinuxEmu)
  - `gpu_driver.cpp` (UsrLinuxEmu)
  - `cmd_cuda.cpp` (TaskRunner)
  - `cuda_scheduler.hpp/.cpp` (TaskRunner)
  - `DOC-01~04` (TaskRunner)
  - `architecture_design.md` (UsrLinuxEmu)

### 2. 关键发现

**严重问题**: UsrLinuxEmu 内部存在**三套**不同的 ioctl 命名：

| 文件 | 前缀 | Magic |
|------|------|-------|
| `cuda_ioctl.h` | `CUDA_IOCTL_*` | `'C'` |
| `ioctl_gpgpu.h` | `GPGPU_*` | `'g'` |
| `architecture_design.md` | `GPU_*` | `'G'` |

**TaskRunner 现状**:
- `cmd_cuda.cpp` 使用 `CUDA_IOCTL_*` (via `cuda_ioctl.h`)
- 文档 DOC-01 描述 `GPGPU_*` (不一致)
- CommandTranslator 未实现

**UsrLinuxEmu 现状**:
- `gpu_driver.cpp` 处理 `GPGPU_*` (不是 `CUDA_IOCTL_*`)
- `GPGPU_SUBMIT_PACKET` 是空实现
- `cuda_ioctl.h` 定义了 `CUDA_IOCTL_*` 但未被 `gpu_driver.cpp` 处理

### 3. 架构决策 (Oracle 建议)

| 问题 | 推荐方案 |
|------|---------|
| 命名 | `CUDA_IOCTL_*` (继承现有代码) |
| 粒度 | 保持独立 ioctl |
| CommandTranslator | 放在 TaskRunner 侧 |
| Submodule | Pinned commit（非跟踪分支） |
| 文档位置 | UsrLinuxEmu 作为接口提供方 |

### 4. 计划文件创建

- [x] 创建 `plans/interface-unification-plan.md` - 详细实施计划
- [x] 创建 `plans/findings.md` - 研究发现

## 待完成工作

### Phase 0: 接口现状审计
- [ ] 确认 `cuda_ioctl.h` 是唯一对外接口
- [ ] 确认 `ioctl_gpgpu.h` 是内部实现还是废弃
- [ ] 确认 `gpu_driver.cpp` 与 `cuda_ioctl.h` 的一致性
- [ ] 输出审计报告

### Phase 1: 统一接口规范定义
- [ ] 确定 `CUDA_IOCTL_*` 为标准
- [ ] 补充缺失的 `GET_DEVICE_INFO` (0x00)
- [ ] 更新文档

### Phase 2: 代码实现对齐
- [ ] 修改 `gpu_driver.cpp` 处理 `CUDA_IOCTL_*`
- [ ] 实现 CommandTranslator
- [ ] 编译验证

### Phase 3: Submodule 和 CI
- [ ] 配置 `external/UsrLinuxEmu` submodule
- [ ] 更新 CI 配置

### Phase 4: 文档最终化
- [ ] 统一接口文档位置
- [ ] 更新所有文档引用

## 错误记录

| 日期 | 错误 | 尝试 | 解决 |
|------|------|------|------|
| - | 尚未开始实施 | - | - |

## 关键风险

1. **UsrLinuxEmu 内部三套 ioctl**：需要明确哪套是标准
2. **gpu_driver.cpp 不处理 CUDA_IOCTL_***：需要修改实现
3. **SUBMIT_PACKET 空实现**：批量提交功能缺失

## 下一步行动

1. **立即**: 与 UsrLinuxEmu 团队确认接口规范（Phase 0 审计）
2. **本周**: 完成 Phase 1（统一接口规范定义）
3. **下周**: 完成 Phase 2-4（代码实现、Submodule、文档）
