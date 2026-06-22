# Design: h3-phase2-management

> **依赖**: `proposal.md` 已完成
> **作用**: 解释 HOW 在 H-2.5 抽象之上实现 5 个 Phase 2 ioctl wrapper
> **状态**: ⚠️ DRAFT — D1-D5 已 FINALIZED，待 review 后激活

## Context

### H-2.5 前置依赖（H-2.5 必须先完成）

| 任务组 | 状态 | 说明 |
|--------|------|------|
| `IGpuDriver` 抽象接口（H-2.5 定义） | ⏳ H-2.5 范围 | 纯虚方法集（H-1 已有 + H-3 新增 5 个）|
| `GpuDriverClient` 实现 `IGpuDriver`（H-2.5 改）| ⏳ H-2.5 范围 | 把现有方法挪到 `IGpuDriver` 实现 |
| `CudaStub` 实现 `IGpuDriver`（H-2.5 新增）| ⏳ H-2.5 范围 | 5 个 mock 方法（H-3 填实现）|
| `MockGpuDriver` 测试夹具（H-2.5 新增）| ⏳ H-2.5 范围 | `tests/test_gpu_phase2.cpp` 依赖 |
| `CudaScheduler` 构造函数加 `IGpuDriver*` 注入 | ⏳ H-2.5 范围 | DI 解耦 CudaScheduler 与 GpuDriverClient |
| CLI `init_gpu_client()` 死调用修复 | ⏳ H-2.5 范围 | `cli_main.cpp` 修复 |

**H-3 之前**：`CudaScheduler` 仅依赖 `CudaStub`（不连通 GPU），`GpuDriverClient` 是 dead code。
**H-3 之后**：`CudaScheduler` 可注入 `GpuDriverClient`（联调真 GPU）或 `MockGpuDriver`（单测）。

### UsrLinuxEmu Phase 2 ioctl 现状（`gpu_ioctl.h` line 157-218）

| ioctl | magic | struct | 用途 |
|-------|-------|--------|------|
| `GPU_IOCTL_CREATE_VA_SPACE` | 0x30 | `gpu_va_space_args {page_size, flags, va_space_handle}` | 创建 VA Space，返回 handle |
| `GPU_IOCTL_DESTROY_VA_SPACE` | 0x31 | `gpu_va_space_handle_t`（裸标量）| 销毁 VA Space |
| `GPU_IOCTL_REGISTER_GPU` | 0x32 | `gpu_register_gpu_args {va_space_handle, gpu_id, flags}` | VA Space ↔ GPU 绑定 |
| `GPU_IOCTL_CREATE_QUEUE` | 0x40 | `gpu_queue_args {va_space_handle, queue_type, priority, ring_buffer_size, queue_handle, doorbell_pgoff}` | 创建 Queue |
| `GPU_IOCTL_DESTROY_QUEUE` | 0x41 | `gpu_queue_handle_t`（裸标量）| 销毁 Queue |

### TaskRunner 当前 API surface

| 方法 | ioctl | 来源 | H-3 改动 |
|------|-------|------|----------|
| `submit_batch()` | PUSHBUFFER_SUBMIT_BATCH | H-1 + main | 不改 |
| `submit_memcpy()` / `submit_launch()` | PUSHBUFFER_SUBMIT_BATCH | main | 不改 |
| `gpu_alloc()` / `gpu_free()` | ALLOC_BO / FREE_BO | main | 不改 |
| `get_device_info()` | DEVICE_INFO | main | 不改 |
| `setCurrentVASpace()` / `getCurrentVASpace()` | (无，独立 setter) | **H-1** | 不改（保留兼容）|
| `create_va_space()` / `destroy_va_space()` | CREATE/DESTROY_VA_SPACE | ❌ | **H-3 新增** |
| `register_gpu()` | REGISTER_GPU | ❌ | **H-3 新增** |
| `create_queue()` / `destroy_queue()` | CREATE/DESTROY_QUEUE | ❌ | **H-3 新增** |

## Goals / Non-Goals

### Goals

- `IGpuDriver` 在 `GpuDriverClient` 上真实实现 5 个 Phase 2 ioctl wrapper
- `CudaStub` 同步 mock 实现 5 个方法（递增 handle）
- `MockGpuDriver`（H-2.5 提供）+ 新增 `tests/test_gpu_phase2.cpp` 覆盖 10 个测试用例
- CLI 新增 2 个 subcommand（`cuda_va_space` / `cuda_queue`），不再 dead code
- R2 mapping 契约在测试中显式验证（`stream_id == LOW32(queue_handle)`）
- 同步点 S5 在 `sync-plan.md` 中关闭
- ABI 兼容性保持（既有 `submit_batch` / `setCurrentVASpace` 零变化）

### Non-Goals

- **不**创建 `IGpuDriver` 抽象（H-2.5 范围）
- **不**改既有 `submit_batch` / `setCurrentVASpace` 签名
- **不**改 UsrLinuxEmu `gpu_ioctl.h` ABI（TaskRunner 仅消费）
- **不**实现 `MAP_QUEUE_RING` mmap 快速路径（TaskRunner 走 ioctl）
- **不**解决上游 3 个 owner-flagged issue（deferred to **UsrLinuxEmu H-7 ADR**）
- **不**实现 CudaScheduler 层的 VA Space 自动管理（属于更高层抽象，超出 H-3 scope）
- **不**改 H-1 capability（`gpu-pushbuffer-validation`）spec
- **不**改 H-2.5 capability（`gpu-driver-architecture`）spec —— 接口形状不变

## Decisions（D1-D5 FINALIZED）

### D1 — VA Space 生命周期归属 = **C. Caller owns**

**决策**：`create_va_space()` 返回 `uint64_t` handle；`GpuDriverClient` **不**在内部维护 `current_va_space_handle_`（该字段在 H-1 时期存在，H-3 之后**继续保留**为透传缓存，但**不**在 `create_va_space()` 中自动 set）；caller（CudaScheduler / CLI）显式持有 handle 并在 submit 前调用 `setCurrentVASpace()`。

**理由**：
- **支持多 VA Space**：方案 A/B 限制单 VA Space；方案 C 不限制
- **与 H-2.5 抽象一致**：`IGpuDriver::create_va_space()` 纯返回值，无副作用
- **责任清晰**：caller 显式管理生命周期，避免 driver 内部状态与 caller 期望不一致
- **测试友好**：MockGpuDriver 不需要跟踪"current VA Space"概念

**权衡**：
- 样板代码略多（caller 多写 1-2 行 `setCurrentVASpace()`）
- 但 caller 拥有"何时使用哪个 VA Space"的完全控制权

**与 H-1 的关系**：`GpuDriverClient::current_va_space_handle_` 字段**保留**（H-1 兼容路径），但 H-3 起 caller 必须**显式** `setCurrentVASpace()` 才会触发 H-1 校验。

### D2 — Queue 生命周期 = **B. Explicit create-destroy**

**决策**：`create_queue()` 返回 `uint64_t queue_handle`（monotonic from 1 per R2）；`destroy_queue(queue_handle)` 显式释放。**不**与 stream_id 隐式绑定。

**理由**：
- **细粒度控制**：caller 可创建多个 Queue（如 compute queue + copy queue）并行使用
- **R2 mapping 透明**：caller 拿到 u64 handle 后，submit 时显式用 `(uint32_t)handle` 作 `stream_id`
- **与 UsrLinuxEmu 行为一致**：`gpgpu_device.cpp:412` 用 `next_queue_handle_++` 单调递增，H-3 同步

**备选 A（与 stream_id 1:1 隐式绑定）的拒绝理由**：
- 限制单 Queue，违背 Phase 2 多队列并行目标
- 隐藏 `stream_id` ↔ `queue_handle` 转换，caller 难以调试

### D3 — 方法命名风格 = **B. snake_case**

**决策**：5 个新方法用 snake_case：`create_va_space` / `destroy_va_space` / `register_gpu` / `create_queue` / `destroy_queue`。

**理由**：
- **与 main 方法一致**：`submit_memcpy` / `submit_launch` / `gpu_alloc` / `gpu_free` / `get_device_info` 全部 snake_case
- **避免历史包袱**：H-1 的 `setCurrentVASpace` / `getCurrentVASpace` 是早期 CamelCase 异常，**不**在 H-3 延续
- **C++ 标准库风格**：`std::filesystem::create_directory` 等 STL API 倾向 snake_case

**H-1 例外处理**：`setCurrentVASpace` / `getCurrentVASpace` **不**重命名（H-1 已稳定 + 文档/测试已引用）；H-3 文档明确"新方法 snake_case，老方法 CamelCase 保留"。

### D4 — Handle 存储 = **B. Return only**

**决策**：`GpuDriverClient` / `CudaStub` **不**在内部维护 `unordered_map<handle, metadata>` 之类结构；`create_va_space` / `create_queue` 仅把 ioctl 输出字段透传给 caller。

**理由**：
- **简单**：`GpuDriverClient` 保持无状态（除 `fd_` + `device_path_` + H-1 的 `current_va_space_handle_`）
- **职责分离**：handle ↔ metadata 映射属于 caller 责任（caller 知道 VA Space 用途、Queue 类型）
- **ABI 稳定**：若 driver 内部存 map，caller 可能假设"driver 会自动清理 handle 关联资源"，增加隐式行为

**备选 A（内部 map）的拒绝理由**：
- API 复杂度↑
- 与 D1 决策（caller owns）冲突

### D5 — 默认 VA Space = **B. opt-in**

**决策**：`GpuDriverClient` 构造时**不**自动 `create_va_space()`；`current_va_space_handle_` 默认 0（保留 H-1 sentinel → 跳过校验路径）。

**理由**：
- **H-1 向后兼容**：既有调用方（`submit_batch` 不带 VA Space）走 sentinel 路径，**不**被破坏
- **D1 决策一致**：caller owns → caller 决定何时创建
- **测试隔离**：MockGpuDriver 不需要"构造即 create_va_space"副作用

## 5 Method Signatures（基于 `gpu_ioctl.h`）

### `create_va_space`

```cpp
uint64_t create_va_space(uint32_t flags) {
    if (!is_open()) return 0;  // H-1 sentinel guard
    struct gpu_va_space_args args = {};
    args.page_size = 0;  // 0=4KB; 1=64KB（H-3 暂不暴露 page_size param）
    args.flags = flags;
    if (ioctl(fd_, GPU_IOCTL_CREATE_VA_SPACE, &args) < 0) {
        std::cerr << "GpuDriverClient: GPU_IOCTL_CREATE_VA_SPACE failed"
                  << " (errno=" << errno << ")\n";
        return 0;
    }
    return args.va_space_handle;  // 0 = 失败；非零 = 成功
}
```

### `destroy_va_space`

```cpp
int destroy_va_space(uint64_t va_space_handle) {
    if (!is_open()) return -1;
    if (va_space_handle == 0) return -1;  // 守卫：拒绝 sentinel
    if (ioctl(fd_, GPU_IOCTL_DESTROY_VA_SPACE, &va_space_handle) < 0) {
        std::cerr << "GpuDriverClient: GPU_IOCTL_DESTROY_VA_SPACE failed"
                  << " (errno=" << errno << ")\n";
        return -1;
    }
    return 0;
}
```

### `register_gpu`

```cpp
int register_gpu(uint64_t va_space_handle, uint32_t gpu_id, uint32_t flags) {
    if (!is_open()) return -1;
    if (va_space_handle == 0) return -1;  // H-1 sentinel guard
    struct gpu_register_gpu_args args = {};
    args.va_space_handle = va_space_handle;
    args.gpu_id = gpu_id;
    args.flags = flags;
    if (ioctl(fd_, GPU_IOCTL_REGISTER_GPU, &args) < 0) {
        std::cerr << "GpuDriverClient: GPU_IOCTL_REGISTER_GPU failed"
                  << " (errno=" << errno << ")\n";
        return -1;
    }
    return 0;
}
```

### `create_queue`（注意 u64 返回 + 4 输入字段完整）

```cpp
uint64_t create_queue(uint64_t va_space_handle, uint32_t queue_type,
                      uint32_t priority, uint64_t ring_buffer_size) {
    if (!is_open()) return 0;
    if (va_space_handle == 0) return 0;  // H-1 sentinel guard
    if (priority > 100) return 0;  // 业务校验：priority 范围 0-100
    struct gpu_queue_args args = {};
    args.va_space_handle = va_space_handle;
    args.queue_type = queue_type;  // GPU_QUEUE_COMPUTE/COPY/GRAPHICS
    args.priority = priority;
    args.ring_buffer_size = ring_buffer_size;
    if (ioctl(fd_, GPU_IOCTL_CREATE_QUEUE, &args) < 0) {
        std::cerr << "GpuDriverClient: GPU_IOCTL_CREATE_QUEUE failed"
                  << " (errno=" << errno << ")\n";
        return 0;
    }
    return args.queue_handle;  // u64, monotonic from 1 per R2
    // 注：args.doorbell_pgoff 由 ioctl 路径不需要（H-3 走 ioctl，非 mmap）
}
```

### `destroy_queue`

```cpp
int destroy_queue(uint64_t queue_handle) {
    if (!is_open()) return -1;
    if (queue_handle == 0) return -1;  // 守卫
    if (ioctl(fd_, GPU_IOCTL_DESTROY_QUEUE, &queue_handle) < 0) {
        std::cerr << "GpuDriverClient: GPU_IOCTL_DESTROY_QUEUE failed"
                  << " (errno=" << errno << ")\n";
        return -1;
    }
    return 0;
}
```

## R2 Mapping Contract（关键约束）

`create_queue()` 返回的 `uint64_t queue_handle` 满足：

1. **生成侧**（UsrLinuxEmu `gpgpu_device.cpp:412`）：`uint64_t handle = next_queue_handle_++` —— 单调递增，从 1 开始
2. **消费侧**（TaskRunner）：caller 保存完整 u64 handle
3. **submit 侧**（`submit_batch`）：`args.stream_id = (uint32_t)queue_handle`（取低 32 位）
4. **校验侧**（UsrLinuxEmu `gpgpu_device.cpp:260-262`）：
   ```cpp
   const auto& attached = va_spaces_[args->va_space_handle].attached_queues;
   if (attached.find(static_cast<uint64_t>(args->stream_id)) == attached.end()) {
       return -EINVAL;  // stream_id 零扩展后必须在 attached_queues 中
   }
   ```
5. **ioctl 路径不需 `MAP_QUEUE_RING`**：TaskRunner 通过 `submit_batch` 走 `GPFIFO_BASE + hal_doorbell_ring(hal_, args->stream_id)`（`gpgpu_device.cpp:284-300`）

**Type matching warning**：绝不能把 `queue_handle` 截断为 u32 存为全局计数器；绝不能用 caller 自创 stream_id 替代 LOW32(handle)。这两个错误都会触发 UsrLinuxEmu 的 `-EINVAL`。

## Risks / Trade-offs

| Risk | Impact | Mitigation |
|------|--------|------------|
| **R1**: UsrLinuxEmu Phase 2 ioctl ABI 后续扩展 → wrapper 签名需调整 | 中 | 用 `struct X args = {};` 零初始化（与 H-1 一致）；新增字段追加末尾，不改既有偏移 |
| **R2**: 测试基础设施不成熟（`MockGpuDriver` 缺失）| 高 | **H-2.5 前置**提供 `MockGpuDriver` + 测试夹具；H-3 仅写 test cases |
| **R3**: R2 mapping 契约被破坏（caller 截断 / 自创 stream_id）→ 上游 `-EINVAL` 静默失败 | 中 | R6 Requirement 显式验证 `stream_id == LOW32(queue_handle)`；`tests/test_gpu_phase2.cpp` 第 10 case 锁定 |
| **R4**: 上游 3 个 owner-flagged issue 未解决（stream_id u32 类型不匹配 / ioctl 绕过 GpuQueueEmu / attached_queues 弱校验）| 中 | **不**在本 change 解决；`proposal.md` 交叉引用 H-7 ADR 推迟；TaskRunner 遵守 R2 mapping 即可工作 |
| **R5**: H-2.5 抽象层未完成 → H-3 无 `IGpuDriver` 注入点 | 高 | H-3 依赖 H-2.5；`README.md` 与 `tasks.md §1` 显式 prereq 检查 |
| **R6**: `CudaStub` mock 行为与 `GpuDriverClient` 真实行为不一致 → 切换实现时集成测试 fail | 中 | R9 Requirement 显式测试 `IGpuDriver*` 注入路径；切换 `GpuDriverClient` / `CudaStub` 时跑同一组 test cases |
| **R7**: `GpuDriverClient::current_va_space_handle_` 与 D1（caller owns）冲突 | 低 | H-1 字段保留作"透传缓存"，**不**在 `create_va_space()` 中自动 set；caller 必须显式 `setCurrentVASpace()` |

## Migration Plan

### Phase 1: H-2.5 完成性确认

1. 验证 `openspec/changes/archive/h2-5-architecture-foundation/` 已存在
2. 验证 `IGpuDriver` / `MockGpuDriver` / DI 注入在 TaskRunner 仓可编译

### Phase 2: TaskRunner 实现

```bash
cd external/TaskRunner
# 1. include/gpu_driver_client.h: 加 5 个方法（snake_case）
# 2. src/cuda_stub.cpp: 加 5 个 mock 方法（递增 handle）
# 3. include/igpu_driver.hpp: H-2.5 已定义方法签名，H-3 填 GpuDriverClient/CudaStub 实现
# 4. tests/test_gpu_phase2.cpp: 10 个 doctest case
# 5. src/cmd_cuda.cpp: 加 cuda_va_space / cuda_queue subcommand
make -j4 && ./test_cuda_scheduler && ./test_gpu_phase2
git add -A && git commit -m "feat(igpu): Phase 2 VA Space + Queue lifecycle (H-3)"
git push -u origin feat/h3-phase2-wrappers
```

### Phase 3: 跨仓同步（UsrLinuxEmu）

```bash
cd /workspace/project/UsrLinuxEmu
# 1. 更新子模块指针
git add external/TaskRunner
# 2. 激活 openspec change
mv /workspace/project/UsrLinuxEmu/external/TaskRunner/plans/2026-06-19-h3-phase2-openspec-skeleton \
   /workspace/project/UsrLinuxEmu/openspec/changes/h3-phase2-management
# 3. 移除 DRAFT 标记（在 README 与 .openspec.yaml）
# 4. 提交
git add openspec/changes/h3-phase2-management/
git commit -m "feat(h3): H-3 cross-repo sync + openspec change tracking"
```

### Phase 4: 验证 + 归档

```bash
make -j4 && ctest && bash tools/docs-audit.sh --strict
# 预期：test_cuda_scheduler 8/8 + test_gpu_phase2 10/10 = 18 cases pass
openspec archive h3-phase2-management
git add openspec/changes/archive/2026-06-19-h3-phase2-management/
git commit -m "chore(openspec): archive h3-phase2-management"
```

### Rollback

| 阶段 | 回滚命令 |
|------|---------|
| Phase 2 失败 | `git reset HEAD~1 && git restore .` (TaskRunner 仓，未 push) |
| Phase 2 push 后 | `git push --force-with-lease` 回退 + revert PR |
| Phase 3 失败 | `git restore --staged openspec/changes/ external/TaskRunner` |
| Phase 4 archive | `rm -rf openspec/changes/archive/2026-06-19-h3-phase2-management/` |

各阶段独立可逆。

## Open Questions

无（D1-D5 已 FINALIZED，无 TBD 项）。

3 个上游 owner-flagged issue 已交叉引用到 **UsrLinuxEmu H-7 ADR**，TaskRunner 侧**不**解决。
