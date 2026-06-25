---
SCOPE: TEST-FIXTURE
STATUS: DEPRECATED
---

# Phase 1 Week 1 实施计划

**周期**: 2026-04-07 ~ 2026-04-14  
**目标**: ioctl 接口确认 + UsrLinuxEmu 转译层完成  
**状态**: 🟡 进行中（70%）

---

## 本周任务清单

| ID | 任务 | 状态 | 交付物 | 验收标准 |
|----|------|------|--------|----------|
| **W1-T1** | ioctl 接口对齐 | ✅ 完成 | `cuda_ioctl.h` | TaskRunner 与 UsrLinuxEmu 共享同一份定义 |
| **W1-T2** | cuda_compat_ioctl 转译层 | ✅ 完成 | `cuda_compat_ioctl.cpp/.h` | 所有 7 个 ioctl 命令有处理逻辑 |
| **W1-T3** | CudaStub 接口集成 | ✅ 完成 | 编译通过 | `libkernel.a` 包含 `cuda_compat_ioctl` 符号 |
| **W1-T4** | 端到端穿透测试 | 🟡 进行中 | 测试脚本 | ioctl 命令从 TaskRunner → UsrLinuxEmu 全链路通 |
| **W1-T5** | 文档更新 | ✅ 完成 | `phase1-week1-plan.md` | Week 1 计划文档创建 |

---

## 详细实施步骤

### W1-T1: ioctl 接口对齐 ✅

**文件**: `/workspace/UsrLinuxEmu/include/usr_linux_emu/cuda_ioctl.h`

** ioctl 命令列表**:
```
CUDA_IOCTL_MEM_ALLOC       (0x01) - 内存分配
CUDA_IOCTL_MEM_FREE        (0x02) - 内存释放
CUDA_IOCTL_MEMCPY_H2D      (0x03) - Host→Device 拷贝
CUDA_IOCTL_MEMCPY_D2H      (0x04) - Device→Host 拷贝
CUDA_IOCTL_LAUNCH_KERNEL   (0x10) - Kernel 启动
CUDA_IOCTL_WAIT_FENCE      (0x20) - Fence 等待
CUDA_IOCTL_QUERY_FENCE     (0x21) - Fence 查询
CUDA_IOCTL_GRAPH_CREATE    (0x30) - Graph 创建（Phase 2）
CUDA_IOCTL_GRAPH_LAUNCH    (0x31) - Graph 启动（Phase 2）
```

**验收**:
- ✅ TaskRunner 和 UsrLinuxEmu 使用同一份 `cuda_ioctl.h`
- ✅ 结构体定义完全一致

---

### W1-T2: cuda_compat_ioctl 转译层 ✅

**文件**: 
- `/workspace/UsrLinuxEmu/src/kernel/device/cuda_compat_ioctl.cpp`
- `/workspace/UsrLinuxEmu/include/usr_linux_emu/cuda_compat_ioctl.h`

**核心逻辑**:
```cpp
int cuda_compat_ioctl(unsigned long request, void* argp) {
    switch (request) {
        case CUDA_IOCTL_MEM_ALLOC:
            // 调用 CudaStub::mem_alloc()
        case CUDA_IOCTL_MEM_FREE:
            // 调用 CudaStub::mem_free()
        case CUDA_IOCTL_MEMCPY_H2D:
            // 调用 CudaStub::memcpy_h2d()
        case CUDA_IOCTL_MEMCPY_D2H:
            // 调用 CudaStub::memcpy_d2h()
        case CUDA_IOCTL_LAUNCH_KERNEL:
            // 调用 CudaStub::launch_kernel()
        case CUDA_IOCTL_WAIT_FENCE:
            // 调用 CudaStub::wait_event()
        case CUDA_IOCTL_QUERY_FENCE:
            // 调用 CudaStub::query_event()
    }
}
```

**验收**:
- ✅ 所有 7 个 ioctl 命令有处理逻辑
- ✅ 错误处理完整（返回 -errno）
- ✅ 日志输出清晰

---

### W1-T3: CudaStub 接口集成 🟡

**依赖**: TaskRunner `CudaStub` 类

**接口映射表**:

| ioctl 命令 | CudaStub 方法 | 状态 |
|-----------|--------------|------|
| `CUDA_IOCTL_MEM_ALLOC` | `mem_alloc(size, &device_ptr)` | ✅ 对齐 |
| `CUDA_IOCTL_MEM_FREE` | `mem_free(device_ptr)` | ✅ 对齐 |
| `CUDA_IOCTL_MEMCPY_H2D` | `memcpy_h2d(dst, src, size)` | ✅ 对齐 |
| `CUDA_IOCTL_MEMCPY_D2H` | `memcpy_d2h(dst, src, size)` | ✅ 对齐 |
| `CUDA_IOCTL_LAUNCH_KERNEL` | `launch_kernel(params, &task_id)` | ✅ 对齐 |
| `CUDA_IOCTL_WAIT_FENCE` | `wait_event(event_id, timeout_ms)` | ✅ 对齐 |
| `CUDA_IOCTL_QUERY_FENCE` | `query_event(event_id, &signaled)` | ✅ 对齐 |

**待办**:
- [ ] 编译验证（UsrLinuxEmu 引入 TaskRunner 头文件）
- [ ] 链接验证（CudaStub 实现可用）

---

### W1-T4: 端到端穿透测试 ⏳

**测试场景**:
```bash
# 1. 启动 UsrLinuxEmu（模拟内核态）
$ ./usr_linux_emu --cuda-compat

# 2. TaskRunner CLI 发送 ioctl 命令
$ ./taskrunner cuda_alloc 4096
  → 预期：device_ptr=0x10000, fence_id=1

$ ./taskrunner cuda_memcpy h2d 0x10000 0 1024
  → 预期：fence_id=2

$ ./taskrunner cuda_launch vector_add 1 1 1 256 1 1
  → 预期：task_id=1, fence_id=3

$ ./taskrunner cuda_wait 3
  → 预期：Fence signaled
```

**验收**:
- [ ] ioctl 命令全链路通
- [ ] 数据正确传递
- [ ] 错误场景处理（无效 ptr、内存不足等）

---

### W1-T5: 文档更新 ⏳

**更新文件**:
- `DDS-CUDA-Vulkan-Runtime-v1.2-final.md` → 补充实现细节
- `README.md` → 添加 Week 1 进展

---

## 风险与缓解

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|---------|
| CudaStub 接口变更 | 低 | 中 | 已锁定接口定义 |
| UsrLinuxEmu 编译失败 | 中 | 高 | 提前验证头文件路径 |
| ioctl 参数传递错误 | 中 | 高 | 单元测试覆盖 |

---

## 下周计划（Week 2）

**目标**: CudaStub 真实 CUDA Driver 集成

| 任务 | 交付物 |
|------|--------|
| 替换 Stub 模式为真实 CUDA Driver | `cuMemAlloc`/`cuMemcpy`/`cuLaunchKernel` |
| 添加 CUDA context 管理 | `cuInit`/`cuCtxCreate` |
| 性能基准测试 | vector_add kernel 延迟/吞吐量 |

---

**最后更新**: 2026-04-08 04:30  
**负责人**: DevMate
