# tadr-308: IGpuDriver::load_kernel_module (H2D DMA VRAM Load) — Implementation Proposal

## Why

[TaskRunner `tadr-307`](https://github.com/chisuhua/TaskRunner/blob/cdb3633/docs/shared/adr/tadr-307-igpu-driver-kernel-module-extension.md) 提议 3 个 `IGpuDriver` 方法（`load/launch/unload_kernel_module`），对齐 UsrLinuxEmu [ADR-076](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/00_adr/adr-076-gpgpu-kernel-module-ioctl.md) v1 / PTX-EMU ADR-0029 §D8 的 3 fn-ptrs 方案。

2026-08-17 Oracle session `ses_ff2106f84ffeM2oItBEa9iu4hL` 识别该方案违反 [ADR-036 three-way separation](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/00_adr/adr-036-three-way-separation.md)（HAL 桥承担硬件行为提供者职责）。

UsrLinuxEmu [ADR-090 v1](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/00_adr/adr-090-ptxir-via-h2d-dma.md) commit `e03b5a1` 在 UsrLinuxEmu 仓升 ✅ Accepted（Gate #1/#2/#5/#6 ✅），提议 H2D DMA 路径 + 1 fn-ptr 替代 tadr-307 的 3 fn-ptrs。

**[tadr-308](https://github.com/chisuhua/TaskRunner/blob/cdb3633/docs/shared/adr/tadr-308-igpu-driver-vram-load.md)** 是 tadr-307 的对齐重写版（仅追加 1 方法，不删除现有方法）。

## What Changes

### 1. `include/shared/igpu_driver.hpp` 新增 1 方法（append-only per ADR-023 §D4）

```cpp
// 新增方法(默认 -ENOSYS, 不破坏 3 个现有实现者)
virtual int load_kernel_module(const void* image, size_t image_size,
                               uint64_t* out_vram_addr) {
    (void)image; (void)image_size; (void)out_vram_addr;
    return -ENOSYS;
}
```

### 2. `src/umd/libcuda_shim/cu_module.cpp` 适配

`cuModuleLoadData` (:135) 接入 `runtime()->load_kernel_module`:
```cpp
CUresult cuModuleLoadData(CUmodule* module, const void* image) {
    if (!module || !image) return CUDA_ERROR_INVALID_VALUE;
    uint64_t vram_addr = 0;
    int rc = runtime()->load_kernel_module(image, image_size, &vram_addr);
    if (rc != 0) return cuda_error_from_errno(rc);  // NOT static_cast — errno 负值会变非法 CUDA error code
    *module = reinterpret_cast<CUmodule>(vram_addr);  // CUmodule 重定义 = code BO GPU VA
    return CUDA_SUCCESS;
}
```

`cuModuleUnload` (:99) 补 vram_addr → `GPU_IOCTL_FREE_BO` 路径:
```cpp
CUresult cuModuleUnload(CUmodule module) {
    if (!module) return CUDA_ERROR_INVALID_VALUE;
    uint64_t vram_addr = reinterpret_cast<uint64_t>(module);
    int rc = runtime()->free_bo(vram_addr);
    return cuda_error_from_errno(rc);  // NOT static_cast<CUresult>(rc) — errno 负值会变非法 CUDA error code
}
```

### 3. `cu_launch.cpp` **本轮不动**

`GPU_IOCTL_LAUNCH_KERNEL_MODULE` (0x28) 在 UsrLinuxEmu 已 deprecated stub 化返回 -ENOSYS。kernel 实际执行走 `submit_batch` + DISPATCH_KERNEL packet（per ADR-090 v1 §D3.3）。

### 4. 测试扩展

新增 `tests/test_load_kernel_module_standalone.cpp`（TDD 5 步结构）：
- Section 1: 默认实现返回 -ENOSYS
- Section 2: cuModuleLoadData → load_kernel_module 链路
- Section 3: vram_addr → free_bo 链路
- Section 4: 并发 load race
- Section 5: error 注入 (image_size = 0, image = NULL 等)

### 5. tadr-307 标 STALE

`tadr-307-igpu-driver-kernel-module-extension.md` 头部加 STALE 标注（不撤不删）：
```
> **STATUS: STALE (2026-08-18)** — 本文对齐 ADR-076/ADR-0029 §D8 的 3 方法方案
> (load/launch/unload_kernel_module)，已被 ADR-090 v2 §D1 的 1 方法 vram-load 语义取代。
> 后继: tadr-308。
```

### 6. 现有实现者更新（可选，非阻塞）

3 个 IGpuDriver 实现者暂不实现 `load_kernel_module`（默认 -ENOSYS 不报错）：
- `include/test_fixture/cuda_stub.hpp` (`:161` submit_batch 实现可后续添加)
- `include/test_fixture/gpu_driver_client.h` (`:300` submit_batch 已实现, IGpuDriver::load_kernel_module 默认 -ENOSYS 即可)
- `tests/test_fixture/mock_gpu_driver.hpp` (mock 实现, 默认 -ENOSYS 即可)

### 7. Oracle 2026-08-18 硬前置项（per `ses_feb85d969ffe0qPwACwwapfXen`）

| Gate | 描述 | 阻塞 |
|---|---|:---:|
| **#7 CudaRuntimeApi 扩方法** | `include/umd/cuda_runtime_api.hpp` 缺 `load_kernel_module`，shim 编译失败 | ⏳ HARD |
| **#8 `cuda_error_from_errno` helper** | `src/umd/libcuda_shim/` 0 命中此函数 | ⏳ HARD |
| **#9 既有测试预期同步** | `tests/umd/test_cuda_shim.cpp:1037` 断言 `NOT_IMPLEMENTED` 需改 `NOT_SUPPORTED` | ⏳ HARD |
| **#10 image_size 来源决策** | CUDA API 不传 size，需 owner 选 PTXIR magic 扫描 or LoadDataEx | ⏳ HARD |
| **#11 CUmodule 命名空间冲突决策** | `cuModuleLoad`(整数) vs `cuModuleLoadData`(VA) 冲突，需 owner 选方案 A/B | ⏳ HARD |
| **#12 `func_to_module` 表清理** | `cuModuleUnload` 未清理 `func_to_*` 表导致悬挂 handle | ⏳ HARD |
| **#13 mutex 位置修订** | `tasks.md T-004` 原拟在 `IGpuDriver` 加 mutex 违反抽象，改为 `GpuDriverClient` 私有 | ⏳ HARD |
| **#14 父仓契约核验** | UsrLinuxEmu 符号链接在 Oracle 沙盒内断裂，ioctl 0x27/HAL #66/ADR-090 v1/PTX-EMU ADR-0029 §D8/CppTLM #19 未独立核验 | ⏳ HARD |

**上述 8 项（#7-#14）必须在 T-002 实施前全部 ✅，否则编译失败或运行时悬挂 handle 风险**。

## Acceptance Gate

| Gate | Owner | 状态 |
|---|---|:---:|
| #1 UsrLinuxEmu ADR-090 v1 ✅ Accepted | UsrLinuxEmu | ✅ |
| #2 CppTLM maintainer ack | CppTLM | ✅ |
| #3 TaskRunner owner ack | TaskRunner | ⏳ **本 change 待 owner 审阅** |
| #4 PTX-EMU HSK-6 联发 | PTX-EMU | 🚫 跟踪 |
| #5 TaskRunner openspec change 通过 | TaskRunner | ⏳ 本 change |
| #6 新增方法 E2E 测试 | TaskRunner | ⏳ |

## Cross-Repo Coordination

| 仓 | 跟踪载体 | 状态 |
|---|---|---|
| UsrLinuxEmu | ADR-090 v1 + annex §E | ✅ Accepted |
| CppTLM | #19 v3.0 RFC | ✅ Gate #2 ack |
| PTX-EMU | HSK-6 公告草稿 | 🚫 待 PTX-EMU owner 发出 |
| TaskRunner | tadr-308 (本 change) | 📋 待 owner 启动 |

## Migration

tadr-307 → tadr-308 迁移策略：
- tadr-307 不撤不删（保留作历史决策记录），头部加 STALE 标注
- tadr-308 是**追加**而非**替换**：1 新方法（默认 -ENOSYS），不动现有 49 方法
- 现有 3 个 IGpuDriver 实现者不受影响（默认体不破坏）
- 增量铺开：tadr-308 Accepted 后, test-fixture 可逐步实现 `load_kernel_module` 而不强制

## Effort

| 任务 | 工作量 |
|---|---|
| tadr-308 文档 | 0.5 天 ✅ 已起草 |
| tadr-307 STALE 标注 | 0.1 天 |
| `include/shared/igpu_driver.hpp` 新增 1 method | 0.2 天 |
| `src/umd/libcuda_shim/cu_module.cpp` 适配 | 0.5 天 |
| `tests/test_load_kernel_module_standalone.cpp` 新增 | 0.5 天 |
| CI 集成测试 + L1 portability check | 0.5 天 |
| **总计** | ~2.3 天 |

## References

- UsrLinuxEmu [ADR-090 v1 commit `e03b5a1`](https://github.com/chisuhua/UsrLinuxEmu/commit/e03b5a1) (✅ Accepted)
- UsrLinuxEmu [ADR-090 v1 commit `37a91b6`](https://github.com/chisuhua/UsrLinuxEmu/commit/37a91b6) (Oracle F-NEW-2 修订)
- TaskRunner [tadr-308](https://github.com/chisuhua/TaskRunner/blob/cdb3633/docs/shared/adr/tadr-308-igpu-driver-vram-load.md) (本 change 配套)
- TaskRunner [tadr-307 STALE](tadr-307-igpu-driver-kernel-module-extension.md)
- TaskRunner [tadr-301 IGpuDriver contract](tadr-301-igpu-driver-contract.md)
- TaskRunner [tadr-107 shared scope policy](tadr-107-shared-infrastructure-boundary.md)
- PTX-EMU [ADR-0029 §D8](https://github.com/chisuhua/PTX-EMU/blob/main/docs/adr/ADR-0029-ptxemu-image-executor.md) (HSK-1 真相源, accepted)
- CppTLM [issue #19](https://github.com/chisuhua/CppTLM/issues/19) (Gate #2 ack, v3.0 RFC)
- Oracle session `ses_fef78854dffeLfDJh7p8ELuMLy` (4 轮评估, 关键修正: 默认体而非纯虚 + 3 vendored 头 + 1 static_assert + DriverWrapper 行号)
- Oracle session `ses_feb85d969ffe0qPwACwwapfXen` (2026-08-18 深度审查: 5 CRITICAL + 5 MAJOR + 4 MINOR, 含 C1 CUmodule 命名空间冲突 / C2 image_size 来源 / C3 CudaRuntimeApi 缺方法 / C4 cuda_error_from_errno 缺失 / C5 测试预期被破坏 / M1 func_to_module 清理 / M3 IGpuDriver 加 mutex 违反抽象)
- Oracle session `ses_feaa41dfaffeVTyUSpzNH5FDx2` (2026-08-18 第二轮调研 + 审查: Phase 1 外部调研 CUDA Driver API + PTX-EMU cpptlm_module.h v2 + UsrLinuxEmu gpu_ioctl.h/gpu_hal.h 真实契约; Phase 2 新增 2 CRITICAL + 6 MAJOR + 3 MINOR, 含 C6 ADR-090 v2→v1 引用错误 / C7 image_size 类型不匹配 / M6 HAL #66 void*args 引用 / M7 PTX-EMU 不参与说明 / M8 ADR-090 v1 §D4 矛盾 / M10 MAX_KERNEL_IMAGE_SIZE / M11 kernel_name 字段缺失重大新发现)