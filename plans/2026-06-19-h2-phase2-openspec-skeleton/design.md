# Design: h2-phase2-implementation

> **依赖**: `proposal.md` 已完成
> **作用**: 解释 HOW 实现 H-2 (Phase 2 wrapper)，而非 WHY (WHY 见 proposal)
> **状态**: ⚠️ DRAFT — D1-D5 决策点未填

## Context

### H-1 + H-1 closeout 完成状态

| 任务组 | 状态 | 引用 |
|--------|------|------|
| `gpu_ioctl.h` 加 `va_space_handle` 字段 | ✅ | `0272970` |
| `handlePushbufferSubmitBatch` 2 段校验 | ✅ | `bf8192f` |
| 4 个测试 (`test_gpu_pushbuffer_validation`) | ✅ | `09ae1b0` |
| TaskRunner `setCurrentVASpace()` opt-in | ✅ | PR #6 (`134fe0d`) |
| 符号链接 `UsrLinuxEmu` 修复 | ✅ | PR #6 (`95136a5`) |
| H-1 openspec archive | ✅ | `f223994` |

### UsrLinuxEmu Phase 2 ioctl 现状（`gpu_ioctl.h` line 157-218）

| ioctl | magic | struct | 用途 |
|-------|-------|--------|------|
| `GPU_IOCTL_CREATE_VA_SPACE` | 0x30 | `gpu_va_space_args` | 创建 VA Space，返回 handle |
| `GPU_IOCTL_DESTROY_VA_SPACE` | 0x31 | `gpu_va_space_handle_t` | 销毁 VA Space |
| `GPU_IOCTL_REGISTER_GPU` | 0x32 | `gpu_register_gpu_args {va_space_handle, gpu_id, flags}` | VA Space ↔ GPU 绑定 |
| `GPU_IOCTL_CREATE_QUEUE` | 0x40 | `gpu_queue_args` | 创建 Queue (绑定 VA Space) |
| `GPU_IOCTL_DESTROY_QUEUE` | 0x41 | `gpu_queue_handle_t` (裸标量) | 销毁 Queue |

### TaskRunner 当前 API surface（`GpuDriverClient`）

| 方法 | ioctl | 来源 |
|------|-------|------|
| `submit_batch()` | PUSHBUFFER_SUBMIT_BATCH | H-1 + main |
| `submit_memcpy()` | PUSHBUFFER_SUBMIT_BATCH | main |
| `gpu_alloc()` / `gpu_free()` | ALLOC_BO / FREE_BO | main |
| `get_device_info()` | DEVICE_INFO | main |
| `setCurrentVASpace()` | (无，独立 setter) | **H-1** (`134fe0d`) |
| `getCurrentVASpace()` | (无，独立 getter) | **H-1** (`134fe0d`) |

**Gap**: 5 个 Phase 2 ioctl 无 wrapper。

## Goals / Non-Goals

### Goals

- TaskRunner `GpuDriverClient` 暴露 5 个 Phase 2 管理方法
- 每个方法签名简洁、错误处理一致（返回 -1 / 错误码）
- 测试覆盖每个 ioctl 调用路径
- 同步点 S5 在 `sync-plan.md` 中关闭
- ABI 兼容性保持（既有调用方零变化）

### Non-Goals

- **不**实现 CudaScheduler 层的 VA Space 自动管理（属于更高层抽象）
- **不**修改 UsrLinuxEmu `gpu_ioctl.h` ABI（TaskRunner 仅消费）
- **不**实现 GPU 内存映射 / GART / 页面表等 Phase 2 内部细节
- **不**改 H-1 主 capability (`gpu-pushbuffer-validation`) 的 spec
- **不**改既有 wrapper 方法签名

## Decisions

### D1 - VA Space 生命周期归属 ❓ TBD

**决策待定**。候选方案：

| 方案 | 描述 | 优点 | 缺点 |
|------|------|------|------|
| **A. CudaScheduler 拥有** | `CudaScheduler` 构造时创建 VA Space，析构时销毁 | 透明、与流式 API 契合 | CudaScheduler 复杂度↑；不支持多 VA Space |
| **B. GpuDriverClient 内部** | `GpuDriverClient` 内部维护 `current_va_space_handle_`（H-1 已有），`createVASpace()` 时更新 | 调用方无需手动 set；类似 H-1 设计 | 单 VA Space 限制 |
| **C. Caller 拥有** | `createVASpace()` 仅返回 handle，caller 负责持有 + 传给 `setCurrentVASpace()` | 最灵活，支持多 VA Space | caller 负担重；样板代码多 |

**建议**：方案 **B**（与 H-1 一致，渐进式扩展）。但方案 C 更灵活。

**待 `proposal §开放问题 1` 决策后填入最终选项与理由。**

### D2 - Queue 生命周期 ❓ TBD

**决策待定**。候选方案：

| 方案 | 描述 |
|------|------|
| **A. 与 stream_id 1:1** | `createQueue(va_space_handle)` 内部隐式绑定下一个 stream_id |
| **B. 显式 create-destroy** | `createQueue()` 返回 `queue_id`，caller 管理；`submit_batch()` 接受 `stream_id` 参数 |

**建议**：方案 **B**（与 H-1 `setCurrentVASpace()` 模式一致；保持细粒度控制）。

**待 `proposal §开放问题 2` 决策后填入最终选项与理由。**

### D3 - 方法命名风格 ❓ TBD

**决策待定**。候选方案：

| 方案 | 例子 | 风格一致性 |
|------|------|-----------|
| **A. CamelCase** | `createVASpace()` | 与 `setCurrentVASpace()` (H-1) 一致 |
| **B. snake_case** | `create_va_space()` | 与 `submit_memcpy()` / `gpu_alloc()` 等 main 方法一致 |

**建议**：方案 **A**（与 H-1 一致；Phase 2 是 H-1 的扩展）。

**待 `proposal §开放问题 3` 决策后填入最终选项与理由。**

### D4 - Handle 存储 ❓ TBD

**决策待定**。候选方案：

| 方案 | 描述 | 优点 | 缺点 |
|------|------|------|------|
| **A. 内部 map** | `GpuDriverClient` 维护 `unordered_map<handle, metadata>` | 自动管理生命周期 | API 复杂度↑ |
| **B. 仅返回值** | `createVASpace()` 返回 handle，caller 存 | 简单、无状态 | caller 责任重 |

**建议**：方案 **B**（最小可用）。如需自动管理，留给 CudaScheduler 层。

**待 `proposal §开放问题 4` 决策后填入最终选项与理由。**

### D5 - 默认 VA Space ❓ TBD

**决策待定**。候选方案：

| 方案 | 描述 |
|------|------|
| **A. 自动创建** | `GpuDriverClient::open()` 时自动 `createVASpace()` 并 set |
| **B. 保持 opt-in** | `GpuDriverClient` 默认 `current_va_space_handle_ = 0`（H-1 现状），caller 显式 create + set |

**建议**：方案 **B**（与 H-1 backward-compat 设计一致；D1-A 若是 CudaScheduler 拥有则此问题自动消失）。

**待 `proposal §开放问题 5` 决策后填入最终选项与理由。**

## Risks / Trade-offs

| Risk | Impact | Mitigation |
|------|--------|------------|
| **R1**: D1-D5 决策延迟 → 本 change 阻塞 | 中 | 强制 review 在 1 周内决定；临时选 B 方案作占位 |
| **R2**: Phase 2 ioctl struct 字段后续扩展 → wrapper 需重新生成 | 低 | 用 `struct X args = {};` 零初始化模式（与 H-1 一致） |
| **R3**: TaskRunner `gpu_ioctl.h` 版本漂移 | 低 | 通过符号链接访问 UsrLinuxEmu（已修复） |
| **R4**: 测试无法 stub `CREATE_VA_SPACE` (handler 实际有副作用) | 中 | 检查 UsrLinuxEmu stub 模式；若不支持，需 test mode 适配 |
| **R5**: CudaScheduler 未来集成 VA Space 自动管理时与 D1 冲突 | 中 | D1 决策保留扩展点（即便选 B，也可后续在 CudaScheduler 层叠加） |

## Migration Plan

### Phase 1: 决策定型 (D1-D5)

1. 召集 review：TaskRunner + UsrLinuxEmu 双边 owner
2. 在 PR review 流程中固化 D1-D5 决策
3. 同步更新本 design.md (移除 ❓ TBD 标记)
4. 更新 `proposal.md §开放问题`

### Phase 2: 实现 (TaskRunner)

```bash
cd external/TaskRunner
# 1. 在 include/gpu_driver_client.h 加 5 个方法
# 2. 在 tests/test_cuda_scheduler.cpp 加 5 个 test cases
# 3. 编辑 plans/sync-plan.md 关闭 S5
make -j4 && ./test_cuda_scheduler   # 8+5=13 cases
git add -A && git commit -m "feat(client): Phase 2 VA Space + Queue wrappers"
git push -u origin feat/h2-phase2-wrappers
```

### Phase 3: 跨仓同步 (UsrLinuxEmu)

```bash
cd /workspace/project/UsrLinuxEmu
git add external/TaskRunner                       # 更新子模块指针
git add openspec/changes/archive/2026-06-19-h2-phase2-implementation/  # archive
git commit -m "fix(h2-closeout): H-2 cross-repo sync + archive git tracking"
```

### Phase 4: 验证 + 归档

```bash
make -j4 && ctest && bash tools/docs-audit.sh --strict
openspec archive h2-phase2-implementation
```

### Rollback

| 阶段 | 回滚命令 |
|------|---------|
| Phase 2 失败 | `git reset HEAD~1 && git restore .` (TaskRunner 仓) |
| Phase 3 失败 | `git restore --staged external/TaskRunner && git restore --staged openspec/changes/` |
| Phase 4 archive | `rm -rf openspec/changes/archive/2026-06-19-h2-phase2-implementation/` |

## Open Questions

5 项 D1-D5 决策点待 review 时确定。每项决策后：
1. 更新 `design.md` 中对应 Decision 段
2. 更新 `proposal.md §开放问题` 移除已决项
3. 更新 `tasks.md` 中"TBD"段
4. 更新 `specs/gpu-phase2-management/spec.md` 中 ADDED Requirements