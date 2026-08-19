---
SCOPE: shared
STATUS: PROPOSED
DATE: 2026-08-09
CHANGE: igpu-driver-kernel-module-extension
RELATED: tadr-301-igpu-driver-contract.md
RELATED: tadr-305-mempool-export-shareable.md
RELATED: UsrLinuxEmu adr-076-gpgpu-kernel-module-ioctl.md (canonical source)
RELATED: PTX-EMU ADR-0029 §D8 HAL extension proposal
---

> **STATUS: STALE (2026-08-18)** — 本文对齐 ADR-076/ADR-0029 §D8 的 3 方法方案
> (load/launch/unload_kernel_module)，已被 [ADR-090 v2 §D1](tadr-308-igpu-driver-vram-load.md) 的 1 方法 vram-load 语义取代。
> 后继: [tadr-308](tadr-308-igpu-driver-vram-load.md)。保留本文作历史决策记录, 不得作为实施依据。
> 关联: chisuhua/UsrLinuxEmu ADR-090 v2 commit `e03b5a1`
> 判例: Oracle session `ses_ff2106f84ffeM2oItBEa9iu4hL` 识别本文 3 方法方案违反 [ADR-036 three-way separation](../../../../docs/00_adr/adr-036-three-way-separation.md)（HAL 桥承担硬件行为提供者职责）。

# TADR-307: IGpuDriver Kernel Module Extension (PTX-EMU Image Executor HAL Backend)

## Context

PTX-EMU ADR-0029 §D8 (2026-08-09 跨仓评审修订) 决定采用 **HAL 扩展方案**集成 PTX-EMU `libptxemu_device.so` 作为 UsrLinuxEmu HAL backend。TaskRunner UMD 通过现有的 System C ioctl 链路消费 PTX-EMU 的 in-memory image execution 能力，**零 PTX-EMU 链接依赖**。

本 TADR 是 consumer-side 对偶文档（per ADR-035 §R3 cross-repo 协议）：
- [UsrLinuxEmu adr-076](../docs/00_adr/adr-076-gpgpu-kernel-module-ioctl.md) — **canonical source**（System C ioctl 编号 + 结构体 + HAL fn-ptr 完整定义）
- [PTX-EMU ADR-0029 §D8](../../../../../PTX-EMU/docs/adr/ADR-0029-ptxemu-image-executor.md#d8-cp-端集成约定--hal-扩展方案usrlinuxemu--ptx-emu-跨仓契约) — 跨仓集成设计（HAL 方案采纳）
- **本 TADR** — TaskRunner 端 IGpuDriver 扩展契约

**模板**：参考 [tadr-305-mempool-export-shareable.md](tadr-305-mempool-export-shareable.md) 模式（同 H-2.5 + tadr-301 + 跨仓 IGpuDriver 扩展 + 姊妹 ADR）。

**触发场景**：
- TaskRunner `cu_module.cpp:135` `cuModuleLoadData` 当前返回 `CUDA_ERROR_NOT_IMPLEMENTED`
- 替换路径：`cuModuleLoadData(image)` → `runtime()->load_kernel_module(image)` → `IGpuDriver::load_kernel_module` → `GpuDriverClient` → `GPU_IOCTL_LOAD_KERNEL_MODULE` (0x27) → UsrLinuxEmu GpgpuDevice → `hal_user.cpp` dlsym `libptxemu_device.so` → `ptxemu_image_load`

**架构边界**（per [ADR-036](../docs/00_adr/adr-036-three-way-separation.md) §Decision line 41）：
- TaskRunner 仓**零 PTX-EMU 链接依赖**——PTX-EMU 仅作为 UsrLinuxEmu HAL 的 implementation detail
- 所有跨边界调用走 UsrLinuxEmu System C ioctl 通道
- GPU 状态唯一来源在 UsrLinuxEmu（`HardwarePullerEmu` + `GpgpuDevice`）

## Decision

### D1: 3 个新 IGpuDriver 纯虚方法（#48-#50，扩展自 tadr-301 当前 47 方法）

```cpp
// include/shared/igpu_driver.hpp（追加，零修改现有 47 方法）
class IGpuDriver {
public:
  // ... 现有 47 个方法（tadr-301 + tadr-305 定义，本 TADR 零修改）...

  /** PTX-EMU image executor HAL backend (per adr-076)
   *  Load PTXIR or PTXIR-Embedded CUBIN bytes into module handle
   *  @param image          PTXIR-Embedded CUBIN or standalone PTXIR bytes
   *  @param image_size     image byte count
   *  @param out_handle     [out] opaque module handle (PTX-EMU 端生成)
   *  @param out_kernel_name [out] kernel name in image (max 256 bytes per PTXIR-EMU ADR-0029 D4)
   *  @return 0 success; negative errno on failure (-EINVAL/-EFAULT/-ENOMEM/-EBUSY/-ENOSYS/-EPROTO/-EIO)
   */
  virtual int load_kernel_module(const void* image, size_t image_size,
                                 uint64_t* out_handle,
                                 char* out_kernel_name, size_t kernel_name_buf_size) {
    return -1;  // default stub: NOT_SUPPORTED
  }

  /** Synchronously launch module kernel (waits until kernel completion)
   *  @param module_handle  from load_kernel_module
   *  @param grid_{x,y,z}   grid dimensions
   *  @param block_{x,y,z}  block dimensions
   *  @param shared_mem_bytes dynamic shared memory size
   *  @param kernel_args    array of void* kernel argument pointers (must outlive call)
   *  @param args_count     kernel_args length
   *  @return 0 success; non-zero cudaError_t mapped to negative errno on failure
   */
  virtual int launch_kernel_module(uint64_t module_handle,
                                   uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
                                   uint32_t block_x, uint32_t block_y, uint32_t block_z,
                                   size_t shared_mem_bytes,
                                   void** kernel_args, size_t args_count) {
    return -1;
  }

  /** Unload module (returns -EBUSY if kernel in-flight)
   *  @return 0 success; -EBUSY in-flight; other negative errno on failure
   */
  virtual int unload_kernel_module(uint64_t module_handle) {
    return -1;
  }
};
```

**方法签名与 [adr-076 §D2](../docs/00_adr/adr-076-gpgpu-kernel-module-ioctl.md#d2-3-个新-hal-fn-ptr666768) HAL fn-ptr 字段对齐**——`load_kernel_module` 多 1 个 `out_kernel_name` 参数（PTXIR v1 single-kernel 暴露给 TaskRunner 的必要信息）。

### D2: 错误码语义（与 adr-076 §D5 一致）

| 返回值 | 触发场景 | TaskRunner 端映射 |
|--------|---------|-------------------|
| `0` | 成功 | 转发 `CUDA_SUCCESS` |
| `-EINVAL` | image_size 为 0 / handle 无效 / 参数非法 | `CUDA_ERROR_INVALID_VALUE` |
| `-EFAULT` | image_ptr / args_ptr 用户态不可读 | `CUDA_ERROR_INVALID_VALUE` |
| `-ENOMEM` | PTX-EMU 端 GPU 状态满 | `CUDA_ERROR_OUT_OF_MEMORY` |
| `-EBUSY` | unload 时 in-flight kernel | `CUDA_ERROR_INVALID_HANDLE` (busy) |
| `-ENOSYS` | `libptxemu_device.so` 未找到（dlsym 三级 fallback 全失败）| `CUDA_ERROR_NOT_SUPPORTED` |
| `-EPROTO` | PTX-EMU ABI version 不匹配 | `CUDA_ERROR_INVALID_PTX` |
| `-EIO` | PTX-EMU 内部错误（ANTLR parse / deserialize / execution 失败）| `CUDA_ERROR_UNKNOWN` |

### D3: 同步 vs 异步语义（与 adr-076 §D6 一致）

**v1 全部同步**：
- `load_kernel_module` 同步（用户态等待 PTX-EMU 解析完成返回 handle）
- `launch_kernel_module` **同步阻塞**（返回时 kernel 已完成）—— 与 TaskRunner `cuLaunchKernel` 现状一致（[`cu_launch.cpp:89-91`](../../src/umd/libcuda_shim/cu_launch.cpp) 走 `runtime()->launch_kernel(name, ...)` 同步阻塞）
- `unload_kernel_module` 同步（立即释放或返回 -EBUSY）

**异步延后到 v2**：fence/callback 机制不在本 TADR 范围。

### D4: shim 调用链（修改 `cu_module.cpp` / `cu_launch.cpp`）

#### D4.1: `cuModuleLoadData` 替换

```cpp
// src/umd/libcuda_shim/cu_module.cpp
// 原 line 135:
extern "C" CUresult cuModuleLoadData(CUmodule* module, const void* image) {
  if (!module || !image) return CUDA_ERROR_INVALID_VALUE;
  std::lock_guard<std::mutex> lock(async_task::umd::shim::g_handles.mu);

  // Call IGpuDriver extension (new path)
  uint64_t handle = 0;
  char kernel_name[256] = {0};
  int rc = async_task::umd::shim::runtime()->load_kernel_module(
      image, 0 /* size unknown; need size query */,
      &handle, kernel_name, sizeof(kernel_name));
  if (rc < 0) return cuda_error_from_errno(rc);

  *module = reinterpret_cast<CUmodule>(handle);
  async_task::umd::shim::g_handles.mod_to_name[*module] = kernel_name;
  return CUDA_SUCCESS;
}
```

> **注**：`image_size` 参数缺失问题——CU Driver API `cuModuleLoadData` 仅传 `const void* image` 无 size。需要从 image magic 前 4 字节推断 size（PTXIR 头含 total size），或保留 0 表示"读取直到 magic 终止"。详细设计待 TaskRunner owner 确认（建议 task list 包含此调研）。

#### D4.2: `cuModuleUnload` 替换

```cpp
// src/umd/libcuda_shim/cu_module.cpp
// 原 line 99:
extern "C" CUresult cuModuleUnload(CUmodule hmod) {
  std::lock_guard<std::mutex> lock(async_task::umd::shim::g_handles.mu);
  uint64_t handle = reinterpret_cast<uint64_t>(hmod);

  int rc = async_task::umd::shim::runtime()->unload_kernel_module(handle);
  if (rc == -EBUSY) {
    // In-flight kernel — defer cleanup
    async_task::umd::shim::g_handles.mod_to_busy[handle] = true;
    return CUDA_ERROR_INVALID_HANDLE;  // busy
  }
  if (rc < 0) return cuda_error_from_errno(rc);

  // ... 现有清理逻辑 ...
  return CUDA_SUCCESS;
}
```

#### D4.3: `cuLaunchKernel` 维持现状 + 新增 kernel module launch 路径

`cuLaunchKernel` 当前实现（[cu_launch.cpp:62-92](../../src/umd/libcuda_shim/cu_launch.cpp)）已通过 `runtime()->launch_kernel(name, ...)` → `IGpuDriver::launch_kernel` 路径转发（不调新方法）。

新增逻辑：`cuLaunchKernel` 内部按 `func_to_module[f]` 反查 image handle，**优先**调 `IGpuDriver::launch_kernel_module(handle, ...)`（image executor 路径），**fallback** 到 `IGpuDriver::launch_kernel(name, ...)`（legacy name-based 路径）。两者互斥；TaskRunner owner 决定默认行为（建议默认走新路径）。

#### D4.4: 关键不变式

- 现有 47 个 IGpuDriver 方法签名**零修改**（per tadr-301 现有契约）
- 现有 shim 16 个 `cu_*.cpp` 全部继续编译（仅 `cu_module.cpp` + `cu_launch.cpp` 内部逻辑调整）
- `cuda_driver_accessor.hpp` Meyers singleton 模式不变
- `runtime()` lazy accessor 不变

### D5: CudaRuntimeApi 扩展

```cpp
// src/umd/cuda_runtime_api.hpp（追加，零修改现有方法）
class CudaRuntimeApi {
public:
  // ... 现有方法 ...

  /** @see IGpuDriver::load_kernel_module */
  int load_kernel_module(const void* image, size_t image_size,
                         uint64_t* out_handle,
                         char* out_kernel_name, size_t kernel_name_buf_size) {
    return gpu_driver_->load_kernel_module(image, image_size, out_handle,
                                            out_kernel_name, kernel_name_buf_size);
  }

  /** @see IGpuDriver::launch_kernel_module */
  int launch_kernel_module(uint64_t module_handle,
                           uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
                           uint32_t block_x, uint32_t block_y, uint32_t block_z,
                           size_t shared_mem_bytes,
                           void** kernel_args, size_t args_count) {
    return gpu_driver_->launch_kernel_module(module_handle,
                                              grid_x, grid_y, grid_z,
                                              block_x, block_y, block_z,
                                              shared_mem_bytes,
                                              kernel_args, args_count);
  }

  /** @see IGpuDriver::unload_kernel_module */
  int unload_kernel_module(uint64_t module_handle) {
    return gpu_driver_->unload_kernel_module(module_handle);
  }
};
```

### D6: GpuDriverClient 转发实现

```cpp
// src/test_fixture/gpu_driver_client.cpp（追加，零修改现有转发）
int GpuDriverClient::load_kernel_module(const void* image, size_t image_size,
                                         uint64_t* out_handle,
                                         char* out_kernel_name, size_t kernel_name_buf_size) {
  if (!is_open()) return -1;
  if (!image || !out_handle || !out_kernel_name) return -EINVAL;

  gpu_load_kernel_module_args args{};
  args.image_ptr = reinterpret_cast<uint64_t>(image);
  args.image_size = image_size;
  args.out_module_handle = 0;
  memset(args.kernel_name, 0, sizeof(args.kernel_name));

  int rc = ioctl(fd_, GPU_IOCTL_LOAD_KERNEL_MODULE, &args);
  if (rc < 0) {
    fprintf(stderr, "GpuDriverClient: GPU_IOCTL_LOAD_KERNEL_MODULE failed (errno=%d)\n", errno);
    return -1;
  }
  *out_handle = args.out_module_handle;
  strncpy(out_kernel_name, args.kernel_name, kernel_name_buf_size - 1);
  out_kernel_name[kernel_name_buf_size - 1] = '\0';
  return 0;
}

// 类似 launch_kernel_module / unload_kernel_module 转发
```

### D7: MockGpuDriver 更新（per ADR-039/305 模式）

```cpp
// src/shared/mock_gpu_driver.hpp（追加，零修改现有 47 方法 mock）
class MockGpuDriver : public IGpuDriver {
public:
  // ... 现有 47 个方法 mock ...

  // 新增 3 个 fn-ptr mock 实现（默认 0 成功；可注入错误码）
  int load_kernel_module(const void* image, size_t image_size,
                         uint64_t* out_handle,
                         char* out_kernel_name, size_t kernel_name_buf_size) override {
    if (out_handle) *out_handle = 0xCAFE0001;  // mock handle
    if (out_kernel_name && kernel_name_buf_size > 0) {
      strncpy(out_kernel_name, "mock_kernel", kernel_name_buf_size - 1);
      out_kernel_name[kernel_name_buf_size - 1] = '\0';
    }
    return 0;
  }
  int launch_kernel_module(uint64_t module_handle,
                           uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
                           uint32_t block_x, uint32_t block_y, uint32_t block_z,
                           size_t shared_mem_bytes,
                           void** kernel_args, size_t args_count) override {
    return 0;
  }
  int unload_kernel_module(uint64_t module_handle) override { return 0; }
};
```

## Consequences

- All IGpuDriver implementations override 3 new methods (GpuDriverClient / CudaStub / MockGpuDriver)
- UsrLinuxEmu HAL provides `GPU_IOCTL_LOAD/LAUNCH/UNLOAD_KERNEL_MODULE` (0x27/0x28/0x29) (per adr-076)
- ABI range expands: 47 → 50 methods on `IGpuDriver`
- TaskRunner `cu_module.cpp:135` `cuModuleLoadData` no longer returns `CUDA_ERROR_NOT_IMPLEMENTED`
- `cuLaunchKernel` gains image-executor fast-path (with legacy fallback)
- Backward compatibility: default IGpuDriver implementation returns -1 (NOT_SUPPORTED),
  so stub-mode callers fail gracefully
- **零 PTX-EMU 链接依赖**——TaskRunner 仓不直接 link `libptxemu_device.so`，仅依赖 UsrLinuxEmu System C ioctl 链路

## Cross-Repo

本 TADR 强依赖以下上游契约 ship：

1. **UsrLinuxEmu [adr-076](../docs/00_adr/adr-076-gpgpu-kernel-module-ioctl.md)** — `GPU_IOCTL_LOAD/LAUNCH/UNLOAD_KERNEL_MODULE` (0x27/0x28/0x29) + HAL fn-ptrs #66/#67/#68（per adr-076 §D1/D2）
2. **PTX-EMU [ADR-0029 §D8](../../../../../PTX-EMU/docs/adr/ADR-0029-ptxemu-image-executor.md#d8-cp-端集成约定--hal-扩展方案usrlinuxemu--ptx-emu-跨仓契约)** — `libptxemu_device.so` + `cpptlm_module.h` shipped + tagged v0.1.0+

跨仓 commit 顺序（canonical in [adr-076 §Migration](../docs/00_adr/adr-076-gpgpu-kernel-module-ioctl.md#migration--实施步骤--跨仓-commit-顺序)）：
```
i. PTX-EMU 仓 ship libptxemu_device.so (Phase 1)
ii. UsrLinuxEmu 仓 ship HAL extension (per adr-076)
iii. TaskRunner 仓 ship IGpuDriver extension (per 本 TADR)
iv. UsrLinuxEmu 仓 bump external/TaskRunner submodule pointer + final integration
```

**反向同步协议**（per ADR-035 §R5.1 + §R3）：本 TADR 创建后必须添加到 TaskRunner `docs/shared/adr/README.md` 索引表 + UsrLinuxEmu `docs/00_adr/README.md` TaskRunner TADR mirror 表。

## Acceptance Gate 关系

本 TADR 由 Proposed → Accepted 必须满足两个前置 gate：

1. **UsrLinuxEmu adr-076 gate**（HARD gate，未通过 → TADR 退回 Proposed）：
   adr-076 状态从 PROPOSED → Accepted（HAL extension shipped）
2. **PTX-EMU ADR-0029 gate**（HARD gate，未通过 → TADR 延后）：
   PTX-EMU ADR-0029 Phase 0 + Phase 1 全部 ship + tag

## Acceptance Items

- [ ] `IGpuDriver` 新增 3 个纯虚方法（load/launch/unload_kernel_module），现有 47 个方法零修改
- [ ] `GpuDriverClient` 3 个新 wrapper 实现（构造 ioctl args + 转发）
- [ ] `CudaRuntimeApi` 3 个新方法实现（直接转发到 IGpuDriver）
- [ ] `MockGpuDriver` 3 个新 mock 实现（默认 0 成功；可注入错误码）
- [ ] `cu_module.cpp::cuModuleLoadData` 替换 `CUDA_ERROR_NOT_IMPLEMENTED` → `load_kernel_module` 调用
- [ ] `cu_module.cpp::cuModuleUnload` 新增 busy 检查（-EBUSY → CUDA_ERROR_INVALID_HANDLE）
- [ ] `cu_launch.cpp::cuLaunchKernel` 新增 image-executor fast-path（默认走）+ legacy fallback
- [ ] 现有 47 个 IGpuDriver mock 实现零修改
- [ ] 现有 shim 16 个 `cu_*.cpp` 全部继续编译
- [ ] e2e 测试：mock IGpuDriver → shim → 验证 ioctl 调用契约
- [ ] cross-repo 同步：本 TADR 添加到 TaskRunner `docs/shared/adr/README.md` + UsrLinuxEmu `docs/00_adr/README.md` mirror

## References

- [UsrLinuxEmu adr-076](../docs/00_adr/adr-076-gpgpu-kernel-module-ioctl.md) — canonical source (System C ioctl + HAL fn-ptr 完整定义)
- [PTX-EMU ADR-0029 §D8](../../../../../PTX-EMU/docs/adr/ADR-0029-ptxemu-image-executor.md#d8-cp-端集成约定--hal-扩展方案usrlinuxemu--ptx-emu-跨仓契约) — HAL 方案设计
- [tadr-301-igpu-driver-contract.md](tadr-301-igpu-driver-contract.md) — IGpuDriver 现有契约
- [tadr-305-mempool-export-shareable.md](tadr-305-mempool-export-shareable.md) — 姊妹 TADR 模板
- [ADR-035 §R3](../docs/00_adr/adr-035-governance-policy.md) — cross-repo 同步协议
- [ADR-036](../docs/00_adr/adr-036-three-way-separation.md) — 3 区分架构原则
- TaskRunner `cu_module.cpp`：[src/umd/libcuda_shim/cu_module.cpp](../../src/umd/libcuda_shim/cu_module.cpp)
- TaskRunner `cu_launch.cpp`：[src/umd/libcuda_shim/cu_launch.cpp](../../src/umd/libcuda_shim/cu_launch.cpp)
- TaskRunner `igpu_driver.hpp`：[include/shared/igpu_driver.hpp](../../include/shared/igpu_driver.hpp)

---

## 附录 A: 与 adr-076 字段对齐表

| 本 TADR 字段 | UsrLinuxEmu adr-076 字段 | 状态 |
|------------|---------------------------|------|
| `load_kernel_module(image, size, out_handle, out_kernel_name, name_size)` | §D2 HAL fn-ptr #66 (kernel_module_load) | ✅ 一致 |
| `launch_kernel_module(handle, grid, block, args, args_count, shared_mem)` | §D2 HAL fn-ptr #67 (kernel_module_execute) | ✅ 一致 |
| `unload_kernel_module(handle)` | §D2 HAL fn-ptr #68 (kernel_module_unload) | ✅ 一致 |
| GPU_IOCTL_LOAD_KERNEL_MODULE (0x27) | §D1 ioctl 编号 | ✅ 一致 |
| 错误码语义 (D2) | adr-076 §D5 | ✅ 一致 |
| 同步语义 (D3) | adr-076 §D6 | ✅ 一致 |
| `cuModuleLoadData` 替换路径 (D4.1) | adr-076 §D3 handle_load_kernel_module | ✅ 一致 |
| `cuModuleUnload` 替换路径 (D4.2) | adr-076 §D3 handle_unload_kernel_module | ✅ 一致 |
| `cuLaunchKernel` 路径 (D4.3) | adr-076 §D3 handle_launch_kernel_module | ✅ 一致 |

---

## 附录 B: 与 PTX-EMU ADR-0029 §D8 字段对齐表

| 本 TADR 字段 | PTX-EMU ADR-0029 §D8 字段 | 状态 |
|------------|---------------------------|------|
| `load_kernel_module` image 字节持有 | §D3 image bytes 私有保存 | ✅ 一致（HAL 端不缓存 image bytes；PTX-EMU 端 deep copy）|
| `launch_kernel_module` 同步阻塞 | §D6 SINGLE-GPU-INSTANCE 假设（HAL 方案下不直接暴露）| ✅ 一致 |
| `unload_kernel_module` -EBUSY 返回 | §D8.5 unload busy 语义 | ✅ 一致 |
| 跨仓 commit 顺序 | §D8.7 跨仓 commit 顺序 | ✅ 一致（PTX-EMU 仓 owner 文档化）|