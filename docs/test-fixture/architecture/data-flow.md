# TaskRunner 数据流图

> **最后更新**: 2026-06-23（H-4.5 docs governance cleanup）
> **关联**: [current.md](./current.md), [TADR-005](../adr/tadr-005-h2-5-igpu-driver-consumer-lens.md)

## CUDA Kernel Launch 完整链路

```
[1] User Code
    cudaLaunchKernel(kernel_func, grid, block, args, stream)
              ↓
[2] CUDA Runtime Stub (cuda_stub.cpp)
    - 编码 GPFIFO entry:
      {type: KERNEL, grid_dim, block_dim, shared_mem, kernel_args_ptr}
    - 调用 CudaScheduler::submit_launch(...)
              ↓
[3] CudaScheduler (cuda_scheduler.cpp)
    - 通过 driver_->submit_launch(stream_id, kernel_index, grid, block)
    - driver_ 是 IGpuDriver* (DI 注入)
              ↓
[4] IGpuDriver 抽象 (3 实现分支)
    ├─ GpuDriverClient::submit_launch()        [生产路径]
    │     ↓
    │   构造 gpu_gpfifo_entry + ioctl(GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH)
    │     ↓
    │   返回 fence_id (int64_t)
    │
    ├─ CudaStub::submit_launch()              [Mock 路径 - 单测]
    │     ↓
    │   内部 state 更新 (queue_map_ / next_queue_handle_)
    │   返回 fake fence_id
    │
    └─ MockGpuDriver::submit_launch()         [Headless 测试夹具]
          ↓
        history() 记录 + 可选 inject_error
              ↓
[5] UsrLinuxEmu GPU Driver Plugin (/dev/gpgpu0)
    - BasicGpuSimulator::execute_kernel()
    - 回调函数模拟 warp 执行
    - 完成后触发 fence signal
              ↓
[6] Application 等待 fence
    cudaStreamSynchronize(stream)
    → cuda_stub.cpp 调用 cuda_scheduler.wait_fence(fence_id)
    → ioctl(GPU_IOCTL_WAIT_FENCE)
    → 阻塞直到 fence signaled
```

## Vulkan Compute Submit 完整链路（Phase 3+ ⏸️）

```
[1] User Code
    vkQueueSubmit(compute_queue, submit_info, fence)
              ↓
[2] Vulkan Compute Stub (Phase 3+, vk_compute_stub.cpp)
    - 遍历 submit_info->pCommandBuffers
    - 对每个 VkCommandBuffer 录制内容编码
    - 构造 SubmitInfo (含 semaphore wait/signal)
              ↓
[3] CudaScheduler (扩展支持 Vulkan)
    - 合并所有 command buffer 的命令序列
    - 注入 semaphore wait 前置依赖
    - 通过 driver_->submit_batch() 提交
              ↓
[4] IGpuDriver + UsrLinuxEmu (同 CUDA 路径)
```

⚠️ **Phase 3 Vulkan 路径尚未实施**——待 H-7 ADR 完成 + Phase 3 启动。

## 测试注入路径

```
Test Code
   ↓
构造 MockGpuDriver (含 history() + inject_error())
   ↓
注入到 CudaScheduler(driver) [DI]
   ↓
调用 CudaScheduler::submit_launch(...)
   ↓
MockGpuDriver 记录调用历史 + 返回预定义结果
   ↓
Test 验证 history() 调用次数 + 参数
```

**11 测试 cases** 验证 DI 路径（`tests/test_gpu_architecture.cpp` 10/11 通过，含 H-2.5 Bonus）。

## Phase 2 lifecycle 数据流（VA Space + Queue）

```
create_va_space(flags)
    ↓
cuda_stub.cpp: cuda_scheduler.create_va_space(flags)
    ↓
IGpuDriver::create_va_space(flags) [3 实现分支]
    ├─ GpuDriverClient: ioctl(GPU_IOCTL_CREATE_VA_SPACE, &args)
    │   args.va_space_handle = output
    │   返回 handle (≥1) 成功，0 失败
    ├─ CudaStub: next_va_space_handle_.fetch_add(1) + va_space_map_[handle]=true
    │   返回 handle (≥1)
    └─ MockGpuDriver: history() 记录 + 返回 fake handle

↓ caller 拿到 va_space_handle

set_current_va_space(handle)
    ↓
IGpuDriver::set_current_va_space(handle)
    └─ GpuDriverClient: current_va_space_handle_ = handle (字段)

↓ caller 继续 create_queue(va_space_handle, ...)

create_queue(va_space_handle, type, priority, ring_size)
    ↓
IGpuDriver::create_queue(...) [3 实现分支]
    ├─ GpuDriverClient: ioctl(GPU_IOCTL_CREATE_QUEUE, &args)
    │   args.queue_handle = output (u64, monotonic from 1 per R2)
    │   返回 u64 handle
    └─ CudaStub: next_queue_handle_.fetch_add(1) + queue_map_[handle]=true

↓ caller 拿到 queue_handle (u64)

submit_batch((uint32_t)queue_handle, ...)  // LOW32 truncation per R2
    ↓
GpuDriverClient::submit_batch: ioctl(GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH)
    args.stream_id = (uint32_t)queue_handle (LOW32)

↓ UsrLinuxEmu 端校验
gpgpu_device.cpp:260-262:
  const auto& attached = va_spaces_[args->va_space_handle].attached_queues;
  if (attached.find(static_cast<uint64_t>(args->stream_id)) == attached.end()) {
      return -EINVAL;
  }
```

详见 [TADR-006](../adr/tadr-006-h3-phase2-consumer-lens.md) §Consumer-Lens 4 项实施细节。

---

**END OF DATA FLOW**
