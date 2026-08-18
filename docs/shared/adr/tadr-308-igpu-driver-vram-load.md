---
**编号**: tadr-308
**SCOPE**:**: shared (跨切面契约，dual review 必需 per tadr-107)
**STATUS**: 🔄 **PROPOSED** (2026-08-18)
**DATE**: 2026-08-18
**AUTHOR**: Sisyphus (UsrLinuxEmu Architecture Team)
**CHANGE**: chisuhua/UsrLinuxEmu ADR-090 v2 commit `37a91b6` → 跨仓协调驱动
**RELATED**:
- UsrLinuxEmu [`ADR-090 v2`](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/00_adr/adr-090-ptxir-via-h2d-dma-v2.md) (canonical)
- TaskRunner [`tadr-307`](tadr-307-igpu-driver-kernel-module-extension.md) (**STALE**, supersession by tadr-308)
- TaskRunner [`tadr-301`](tadr-301-igpu-driver-contract.md) (baseline contract)
- TaskRunner [`tadr-107`](tadr-107-shared-infrastructure-boundary.md) (shared scope review policy)
- PTX-EMU ADR-0029（HSK-1 ABI 真相源，已 ship）
- CppTLM [`#19 v3.0 RFC`](https://github.com/chisuhua/CppTLM/issues/19)（Gate #2 ack）

---

## Context

### tadr-307 STALE 缘由

[`tadr-307`](tadr-307-igpu-driver-kernel-module-extension.md) 提议 3 个 `IGpuDriver` 纯虚方法（`load/launch/unload_kernel_module`），对齐 ADR-076 v1 / PTX-EMU ADR-0029 §D8 的 3 fn-ptrs 方案。2026-08-17 Oracle session `ses_ff2106f84ffeM2oItBEa9iu4hL` 识别该方案违反 [ADR-036 three-way separation](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/00_adr/adr-036-three-way-separation.md)（HAL 桥承担硬件行为提供者职责）。

[ADR-090 v2 commit `e03b5a1`](https://github.com/chisuhua/UsrLinuxEmu/commit/e03b5a1) 在 UsrLinuxEmu 仓升 ✅ Accepted（Gate #1/#2/#5/#6 ✅），提议 H2D DMA 路径 + 1 fn-ptr 替代 tadr-307 的 3 fn-ptrs。tadr-308 是 tadr-307 的对齐重写版（**非**逐步叠加）。

### ADR-090 v2 §C0 仲裁结果

[ADR-090 v2 §C0.2](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/00_adr/adr-090-ptxir-via-h2d-dma-v2.md#c02-仲裁裁决) 仲裁：

1. HSK-1 真相源唯一 = PTX-EMU 仓 `include/cudart/cpptlm_module.h:12-52`（8 函数 ABI，`CPPTLM_MODULE_VERSION 2`）
2. UsrLinuxEmu HAL 不可自创 ABI（per ADR-023 §D4 append-only 治理）
3. **tadr-307 必须撤或重写为 tadr-308**（与 HSK-1 真相源 + ADR-023 不兼容）
4. CppTLM 是被驱动的 dGPU 板卡（per [CppTLM #19 v3.0 RFC](https://github.com/chisuhua/CppTLM/issues/19)）

## Decision

### 1. `IGpuDriver::load_kernel_module` 新方法签名

```cpp
/** @brief 加载 PTXIR image 到 CppTLM VRAM (H2D DMA 路径)
 *
 * 对齐 UsrLinuxEmu ADR-090 v2 §D1 + HAL #66 (`kernel_module_load`)。
 * 默认实现返回 -ENOSYS（不破坏 3 个 现有 IGpuDriver 实现者）。
 *
 * @param image        PTXIR image bytes (host pointer)
 * @param image_size   byte 数
 * @param out_vram_addr OUT: code BO 的 GPU VA (消费方: GPU_IOCTL_LOAD_KERNEL_MODULE 0x27)
 * @return 0 成功; 负 errno 失败
 *
 * @see UsrLinuxEmu plugins/gpu_driver/shared/gpu_ioctl.h:723-779 (0x27 契约)
 * @see UsrLinuxEmu plugins/gpu_driver/hal/gpu_hal.h:370 (HAL #66 kernel_module_load)
 */
virtual int load_kernel_module(const void* image, size_t image_size,
                               uint64_t* out_vram_addr) {
    (void)image; (void)image_size; (void)out_vram_addr;
    return -ENOSYS;
}
```

**为何不用 `= 0` 纯虚**（per Oracle 第 4 轮评估）：
仓内已有 3 个 IGpuDriver 实现者，纯虚会让三处全部编译失败：
- `include/test_fixture/cuda_stub.hpp`
- `include/test_fixture/gpu_driver_client.h`
- `tests/test_fixture/mock_gpu_driver.hpp`

带默认体 `-ENOSYS` 增量铺开，不破坏 test-fixture（tadr-307 给 `unload_kernel_module` 已用同 pattern :78）。

### 2. 不删除任何现有方法

**tadr-307 提议的删除清单作废**——tadr-308 **不删除**任何现有 IGpuDriver 方法，**仅追加** 1 个新方法。理由：
- 现有 49 个虚方法已被多个实现者覆盖，删除会破坏向后兼容
- `submit_batch` (`include/shared/igpu_driver.hpp:191`) 已存在（**真实方法名**, 不是 `submit_pushbuffer_batch`——后者是 PTX-EMU owner review 中的笔误）
- launch/unload kernel module 走 `submit_batch` 路径（DISPATCH_KERNEL packet per ADR-090 v2 §D3.3），不需要新增方法

### 3. tadr-307 标 STALE（不撤不删）

tadr-307 是 PROPOSED 未实施（仓内 IGpuDriver 49 虚方法中无任何 kernel-module 方法，实证），无已 ship 代码需要回滚。**tadr-307 保留作历史决策记录**，头部加 STALE 标注：

```markdown
---
> **STATUS: STALE (2026-08-18)** — 本文对齐 ADR-076/ADR-0029 §D8 的 3 方法方案
> (load/launch/unload_kernel_module)，已被 ADR-090 v2 §D1 的 1 方法 vram-load 语义取代。
> 后继: tadr-308。保留本文作历史决策记录, 不得作为实施依据。
> 关联: chisuhua/UsrLinuxEmu ADR-090 v2 commit e03b5a1
---
```

## Consequences

### 替换链路（cuModuleLoadData 适配）

```cpp
// src/umd/libcuda_shim/cu_module.cpp:135 (现状: strong-symbol override stub)
CUresult cuModuleLoadData(CUmodule* module, const void* image) {
    // 旧实现: NOT_IMPLEMENTED
    // 新实现 (per tadr-308 + ADR-090 v2 §D3):
    uint64_t vram_addr = 0;
    int rc = runtime()->load_kernel_module(image, image_size, &vram_addr);
    if (rc != 0) return rc;
    *module = (CUmodule)vram_addr;  // 重定义 CUmodule = code BO GPU VA
    return CUDA_SUCCESS;
}
```

`CUmodule` 句柄语义重定义 = code BO GPU VA（需在文件头注释 `:9-14` 区域补充契约说明）。

`cuModuleUnload`（`:93` 区域已有 "not loaded via cuModuleLoad" 分支）补 vram_addr → `GPU_IOCTL_FREE_BO` 路径。

`cu_launch.cpp` 本轮**不动**（0x28 LAUNCH 已 deprecated，-ENOSYS per ADR-090 v2 §D2.2）。

### 与既有 49 方法的兼容性

- 新方法 `load_kernel_module` 走 append-only（per ADR-023 §D4）
- 现有 3 个 IGpuDriver 实现者不受影响（默认体 `-ENOSYS` 不破坏现有调用）
- test-fixture (`cuda_stub.hpp`, `gpu_driver_client.h`, `mock_gpu_driver.hpp`) 可逐步实现 `load_kernel_module` 而不强制

### 并发/同步语义

`load_kernel_module` 同步返回（与 tadr-307:102 一致）—— 用户态等待 image 解析完成 + 写入 VRAM 返回。后续 PTX-EMU 实际执行（DISPATCH_KERNEL packet）走 `submit_batch` 异步路径。

## Acceptance Gate

| Gate | Owner | 状态 | 前置 |
|---|---|:---:|---|
| #1 UsrLinuxEmu ADR-090 v2 ✅ Accepted | UsrLinuxEmu | ✅ | (commit `e03b5a1` 已 ship) |
| #2 CppTLM maintainer ack | CppTLM | ✅ | ([CppTLM #19](https://github.com/chisuhua/CppTLM/issues/19) ack 2026-08-18) |
| #3 TaskRunner owner ack | TaskRunner | ⏳ | **本 tadr-308** |
| #4 PTX-EMU HSK-6 联发 | PTX-EMU | 🚫 | ([PTX-EMU #12](https://github.com/chisuhua/PTX-EMU/issues/12) closed, 跟踪中) |
| #5 TaskRunner openspec change 创建 | TaskRunner | ⏳ | **配套** |
| #6 新增方法 E2E 测试 | TaskRunner | ⏳ | test-suite 扩展 |

## Migration

### Phase 1: tadr-308 创建 (本 change)
1. ✅ 本文档创建 (`docs/shared/adr/tadr-308-igpu-driver-vram-load.md`)
2. ⏳ tadr-307 头部加 STALE 标注 (上面 §Decision.3 内容)
3. ⏳ `include/shared/igpu_driver.hpp` 新增 1 method (append-only)
4. ⏳ `src/umd/libcuda_shim/cu_module.cpp` 适配 (cuModuleLoadData 接 load_kernel_module)
5. ⏳ 新增 E2E 测试 (test_load_kernel_module_standalone)

### Phase 2: 后续 (本 tadr-308 Accepted 后)
6. ⏳ 等 PTX-EMU HSK-6 公告发出 + CppTLM P0-1 门禁完成
7. ⏳ UsrLinuxEmu submodule bump + Mode B E2E 测试

## Cross-Repo Sync

| 仓 | 跟踪载体 | 当前状态 |
|---|---|---|
| UsrLinuxEmu | ADR-090 v2 + annex §E | ✅ Accepted (commit `e03b5a1` + `37a91b6`) |
| CppTLM | #19 v3.0 RFC | ✅ Gate #2 ack 2026-08-18 |
| PTX-EMU | HSK-6 公告草稿 | 🚫 [PTX-EMU #12](https://github.com/chisuhua/PTX-EMU/issues/12) closed, 待 PTX-EMU owner 发出 commit |
| TaskRunner | tadr-308 (本文件) + openspec change | 📋 本 change 待 owner 启动 |

## References

- UsrLinuxEmu [ADR-090 v2](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/00_adr/adr-090-ptxir-via-h2d-dma-v2.md) (canonical)
- UsrLinuxEmu [annex §E 跟踪表](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/05-advanced/adr-090-cross-repo-coordination.md) (`Gate #2 ✅`)
- Oracle session `ses_fef78854dffeLfDJh7p8ELuMLy` (v2 决策 + 4 轮评估)
- Oracle session `ses_ff2106f84ffeM2oItBEa9iu4hL` (v1 启动，识别 ADR-076 v1 违规)
- CppTLM [issue #19](https://github.com/chisuhua/CppTLM/issues/19) (Gate #2 ack)
- PTX-EMU [issue #12](https://github.com/chisuhua/PTX-EMU/issues/12) (Gate #3 跟踪, closed)
- TaskRunner [issue #10](https://github.com/chisuhua/TaskRunner/issues/10) (Gate #4 跟踪, closed)
- TaskRunner `include/shared/igpu_driver.hpp:191` (`submit_batch` 真实方法名, 不是 `submit_pushbuffer_batch`)
- TaskRunner `src/umd/libcuda_shim/cu_module.cpp:135` (cuModuleLoadData 适配点)
- TaskRunner `src/umd/libcuda_shim/cu_module.cpp:93` (cuModuleUnload 分支点)
- UsrLinuxEmu `plugins/gpu_driver/shared/gpu_ioctl.h:723-779` (0x27 GPU_IOCTL_LOAD_KERNEL_MODULE 契约)
- UsrLinuxEmu `plugins/gpu_driver/hal/gpu_hal.h:370` (HAL #66 kernel_module_load)