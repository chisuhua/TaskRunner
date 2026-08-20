---
SCOPE: shared
STATUS: PROPOSED
DATE: 2026-08-09
CHANGE: igpu-driver-kernel-module-extension
RELATED: tadr-301-igpu-driver-contract.md
RELATED: tadr-305-mempool-export-shareable.md
RELATED: UsrLinuxEmu adr-076-gpgpu-kernel-module-ioctl.md (canonical source, since 🚫 Superseded by ADR-090 v2)
RELATED: PTX-EMU ADR-0029 §D8 HAL extension proposal (since superseded by ADR-090 v2 §C0 仲裁)
SUPERSEDED_BY: [tadr-308](./tadr-308-igpu-driver-vram-load.md) (canonical, current)
---

> **STATUS: STALE (2026-08-18, slimmed 2026-08-20)** — 本文对齐 ADR-076/ADR-0029 §D8 的 3 方法方案
> (load/launch/unload_kernel_module)，已被 [ADR-090 v2 §D1](tadr-308-igpu-driver-vram-load.md) 的 1 方法 vram-load 语义取代。
> 后继: [tadr-308](./tadr-308-igpu-driver-vram-load.md) — **本文仅保留历史决策摘要，不得作为实施依据。**
> 判例: Oracle session `ses_ff2106f84ffeM2oItBEa9iu4hL` 识别本文 3 方法方案违反 ADR-036（HAL 桥承担硬件行为提供者职责）。

# TADR-307: IGpuDriver Kernel Module Extension (PTX-EMU Image Executor HAL Backend) — HISTORICAL ONLY

## 历史决策摘要（per ADR-035 R2.4）

### 原方案（已 STALE）
**追加 3 个 IGpuDriver 纯虚方法**（per UsrLinuxEmu ADR-076 + PTX-EMU ADR-0029 §D8）：

```cpp
class IGpuDriver {
  virtual int load_kernel_module(const void* image, size_t image_size,
                                 uint64_t* out_handle,
                                 char* out_kernel_name, size_t kernel_name_buf_size) = 0;
  virtual int launch_kernel_module(uint64_t module_handle,
                                   uint32_t grid_x/y/z, uint32_t block_x/y/z,
                                   size_t shared_mem_bytes,
                                   void** kernel_args, size_t args_count) = 0;
  virtual int unload_kernel_module(uint64_t module_handle) = 0;
};
```

调用链（失败方案）：`cuModuleLoadData` → `runtime()->load_kernel_module(image, ...)` → `IGpuDriver` → `GPU_IOCTL_LOAD_KERNEL_MODULE (0x27)` → UsrLinuxEmu GpgpuDevice → `hal_user.cpp` dlsym `libptxemu_device.so` → `ptxemu_image_load`。

### 失败原因（Oracle session `ses_ff2106f84ffeM2oItBEa9iu4hL`）

1. **架构层次违规**（ADR-036）：`hal_user.cpp` 让 HAL 桥直接 dlopen 外部硬件行为库 + 同步执行 kernel + handle 分配 + 错误码映射 → HAL 从"桥"变成"硬件行为提供者"
2. **launch 入口方向错位**：`PtxEmuDriverShim.h:32-45` 7 方法 vtable **无 launch 入口**（旧 D8 直链方案 bug）
3. **3 个反向依赖符号**：ptxsim core 引用 cudart/cuda_driver.h，logger.cpp 引用 ptx_interpreter.h 的 `g_gpu_context`

### 替代决策链

| ADR | 关键变更 | 状态 |
|-----|---------|------|
| UsrLinuxEmu [ADR-090 v2](../docs/00_adr/adr-090-ptxir-via-h2d-dma-v2.md) §D1 | HAL fn-ptrs 3→1（#66 保留改 VRAM-load 语义，#67/#68 deprecated stub）；ioctl 0x27 删除 `kernel_name[256]` 字段 | ✅ Accepted (canonical) |
| UsrLinuxEmu [ADR-090 v2](../docs/00_adr/adr-090-ptxir-via-h2d-dma-v2.md) §C0 | 8 函数 ABI（`cpptlm_module.h`）作为 HSK-1 真相源；TaskRunner 仅追加 1 个 IGpuDriver 方法 | ✅ Accepted |
| PTX-EMU [ADR-0028](../../../../../PTX-EMU/docs/adr/ADR-0028-multi-kernel-manifest.md) | ManifestSection 扩展 `kernels[]` 向量，解除 v1 单 kernel 限制 | ✅ Accepted |
| TaskRunner [tadr-308](./tadr-308-igpu-driver-vram-load.md) | Append-only 1× `load_kernel_module(image, image_size, *out_vram_addr)`；CUmodule = GPU VA；image_size 从 24B PTXIR header 推断；kernel_name 应用在 `cuModuleGetFunction` 传入 | 🔄 Proposed (current) |

## 维护说明

- 本文件保留作**历史决策记录**（per ADR-035 R2.4：占位状态可标记为 STALE 而非删除）
- 任何后续任务请直接关联 [tadr-308](./tadr-308-igpu-driver-vram-load.md)
- 不要在本文件追加新决策 — 当前真值源在 tadr-308
- 详细 3 方法提案内容（`D1`/`D2`/`D3`/`D4.1`/`D4.2` 等）已在 tadr-308 Oracle session `ses_fe0443831ffenUxpEQxZqWE8Cp` (2026-08-20) 验证为不可实施架构，**归档处理**

## References

- [tadr-308 IGpuDriver VRAM-Load Extension](./tadr-308-igpu-driver-vram-load.md) — 当前真值源
- [UsrLinuxEmu ADR-090 v2 §D1](../docs/00_adr/adr-090-ptxir-via-h2d-dma-v2.md#d1-8-函数-abi-全量采纳) — HAL #66 重定义为 VRAM-load
- [PTX-EMU ADR-0028](../../../../../PTX-EMU/docs/adr/ADR-0028-multi-kernel-manifest.md) — 多 kernel manifest
- [Oracle session `ses_ff2106f84ffeM2oItBEa9iu4hL`](https://oracle-sessions/ses_ff2106f84ffeM2oItBEa9iu4hL) — 2026-08-17 识别 HAL 层次违规
- [Oracle session `ses_fe0443831ffenUxpEQxZqWE8Cp`](https://oracle-sessions/ses_fe0443831ffenUxpEQxZqWE8Cp) — 2026-08-20 验证 STALE 归档方案