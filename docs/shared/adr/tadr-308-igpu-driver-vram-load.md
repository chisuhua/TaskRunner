---
SCOPE: shared
STATUS: PROPOSED
DATE: 2026-08-18
CHANGE: igpu-driver-vram-load
RELATED: tadr-301-igpu-driver-contract.md
RELATED: tadr-307-igpu-driver-kernel-module-extension.md (STALE)
RELATED: tadr-107-shared-infrastructure-boundary.md
RELATED: UsrLinuxEmu adr-090-ptxir-via-h2d-dma-v2.md
RELATED: PTX-EMU ADR-0029
---

> **AUTHOR**: Sisyphus (UsrLinuxEmu Architecture Team)
> **CHANGE-DRIVER**: chisuhua/UsrLinuxEmu ADR-090 v2 commit `37a91b6` (Oracle F-NEW-2 修订) → 跨仓协调驱动

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
 * 对齐 UsrLinuxEmu [ADR-090 v2 §D1](../../../../docs/00_adr/adr-090-ptxir-via-h2d-dma-v2.md#d1-8-函数-abi-全量采纳) + ioctl 0x27 struct `gpu_load_kernel_module_args` (per `plugins/gpu_driver/shared/gpu_ioctl.h:740-750`)。
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
virtual int load_kernel_module(const void* image, uint64_t image_size,
                               uint64_t* out_vram_addr) {
    // C7 修订: 字段类型 size_t → uint64_t 对齐 ioctl 0x27 真实契约 (u64)
    // M6 修订: 引用改为 "ioctl 0x27 struct", 不是 HAL #66 (HAL #66 实际是 void*args 单参数)
    // M10 修订: MAX_KERNEL_IMAGE_SIZE 边界检查 (ioctl 0x27 强制 [1, 64MB])
    // m6 修订: out_vram_addr NULL 检查
    if (!out_vram_addr) return -EFAULT;
    if (!image) return -EINVAL;
    if (image_size == 0 || image_size > MAX_KERNEL_IMAGE_SIZE) return -EINVAL;
    (void)image; (void)image_size;
    return -ENOSYS;
}
```

> **⚠️ Oracle C2 警告（2026-08-18 session `ses_feb85d969ffe0qPwACwwapfXen` + 修订 2026-08-20 session `ses_fe0443831ffenUxpEQxZqWE8Cp`）**：
> CUDA Driver API `cuModuleLoadData(CUmodule *module, const void *image)` **不传 size 参数**，
> 但 `load_kernel_module` 签名要求 `image_size`。owner 必须在 apply change 前决策：
>
> **方案 1（采纳，per Oracle A′ 修订 2026-08-20）**：扫描 PTXIR 24B header 推断 size
> - PTXIR v4 header（per [PTX-EMU `include/ptx_ir/ptxir_format.h:53-62`](../../../../../PTX-EMU/include/ptx_ir/ptxir_format.h)）:
>   - `magic[4] = {'P','T','X','I'}` + `version u16` + `flags u16` + `section_count u16` + `reserved u16` + **`string_table_offset u32` + `string_table_size u32`** + `header_size u32` = 24B
>   - **layout 字符串表在尾部**（per `ptxir_format.h:12` Header comment），所以 `total_size = string_table_offset + string_table_size`
> - shim 加私有 `ptxir_total_size(const void* image)` helper（**注意**：PTXIR 当前版本 4，非"1 or 2"）
> - cubin/fatbinary 用户需改用 `cuModuleLoadDataEx`（含 size + options 数组）
>
> **方案 2（保守）**：所有用户必须改用 `cuModuleLoadDataEx` 显式传 size
> - shim `cuModuleLoadData` 仍返回 `CUDA_ERROR_INVALID_VALUE` 引导用户改 API
>
> **✅ owner 决策（2026-08-18 Sisyphus, Oracle A′ 确认 2026-08-20）**：选 **方案 1（PTXIR header 扫描 — 24B + string_table 尾部）**。
> - 实施位置：`src/umd/libcuda_shim/ptxir_parser.hpp` 新增 `ptxir_total_size(const void* image)` 私有 helper
> - non-PTXIR 格式（cubin/fatbinary）返回 `CUDA_ERROR_INVALID_VALUE` 引导用户改用 `cuModuleLoadDataEx`
> - 实施任务：[tasks.md T-011 §A](../changes/2026-08-18-tadr-308-igpu-driver-vram-load/tasks.md#t-011-决策点代码实施汇总)

> **⚠️ Oracle 2026-08-18 第二轮审查关键警告（session `ses_feaa41dfaffeVTyUSpzNH5FDx2`）**：
>
> **C6 (跨仓引用警告 - 已修订)**：tadr-308 §Context/§Reference 引用 `adr-090-ptxir-via-h2d-dma-v2.md` — **2026-08-20 Oracle 验证 v2 文件**存在且为 canonical（✅ Accepted，v1 🚫 Superseded by v2）。原"v2 404"断言为假，已删除。
>
> **C7 (字段类型不匹配)**：原 `size_t image_size` 与 ioctl 0x27 真实 `u64 image_size` 不匹配，已修订。
>
> **M6 (HAL #66 引用准确化)**：`plugins/gpu_driver/hal/gpu_hal.h:370` HAL #66 签名是 `int (*kernel_module_load)(void *ctx, void *args)`（void*args 单参数）。`args` 实际是 `struct gpu_load_kernel_module_args*`（ioctl 0x27 struct 复用）。tadr-308 §Decision 1.1 抽象层用 3 参数是 OK 的（per Oracle session `ses_fe0443831ffenUxpEQxZqWE8Cp` 验证），但引用 canonical 是 **ioctl 0x27 struct `gpu_load_kernel_module_args`** (`gpu_ioctl.h:740-750`)。
>
> **M7 (PTX-EMU 关系澄清)**：tadr-308 §Decision 1.1 **不调 PTX-EMU Image Executor**（per ADR-090 v2 §D3 "PTX-EMU 移出 UsrLinuxEmu 进程，CppTLM 接管 Mode B"）。
> 真实路径：ioctl 0x27 → driver H2D DMA 写入 CppTLM VRAM → 返回 `out_vram_addr`。PTX-EMU 8 个 ABI 函数（`ptxemu_image_load/execute/unload/kernel_name` 等，per [ADR-0029 §D1](https://github.com/chisuhua/PTX-EMU/blob/main/docs/adr/ADR-0029-ptxemu-image-executor.md#d1-新-abi-header--cpptlm_moduleh) `cpptlm_module.h:12-52`）**仅由 CppTLM 调用**（Mode B PtxEmuSubmodule façade，per ADR-090 v2 §D3.5）。TaskRunner UMD **零 PTX-EMU 链接依赖**（per ADR-036）。
>
> **M10 (MAX_KERNEL_IMAGE_SIZE)**：ioctl 0x27 handler 强制 `image_size ∈ [1, MAX_KERNEL_IMAGE_SIZE]` 其中 `MAX_KERNEL_IMAGE_SIZE = 64ULL * 1024 * 1024` (64MB)。
> 超限返回 `-EINVAL`。UMD 侧应在 `load_kernel_module` 默认体提前校验以提供更好错误信息。
>
> **M11 (kernel_name 字段缺失 — Oracle 修订 2026-08-20 A′)**：ioctl 0x27 已删 `kernel_name` 字段（per ADR-090 v2 §D1.2 表，**"❌ 不经 HAL / ❌ 不经 ioctl"**）。tadr-308 §Decision 1.1 也无 kernel_name 输出。
> **解决方案（Oracle A′）**：**kernel_name 由应用在 `cuModuleGetFunction(&fn, mod, "name")` 调用时传入**（per `cu_module.cpp:76-97` 已如此工作，零 ABI 变更，零 PTXIR 解析）。详见下方"### 1.5. kernel_name 解析路径"。

**为何不用 `= 0` 纯虚**（per Oracle 第 4 轮评估）：
仓内已有 3 个 IGpuDriver 实现者，纯虚会让三处全部编译失败：
- `include/test_fixture/cuda_stub.hpp`
- `include/test_fixture/gpu_driver_client.h`
- `tests/test_fixture/mock_gpu_driver.hpp`

带默认体 `-ENOSYS` 增量铺开，不破坏 test-fixture（tadr-307 给 `unload_kernel_module` 已用同 pattern :78）。

### 1.5. `kernel_name` 解析路径（M11 — Oracle A′ 修订 2026-08-20）

> **问题（原始）**：ioctl 0x27 删除 `kernel_name[256]` 字段（per ADR-090 v2 §D1.2 表，**"❌ 不经 HAL / ❌ 不经 ioctl"**）后，UMD 端 TaskRunner 怎么知道 PTXIR image 包含哪个 kernel 名？
> 真实 CUDA 用法：`cuModuleGetFunction(&fn, mod, "kernel_name")` 需要知道字符串名。
>
> **Oracle A′ 决策（2026-08-20 session `ses_fe0443831ffenUxpEQxZqWE8Cp`）**：
> **kernel_name 由应用在 `cuModuleGetFunction(&fn, mod, "name")` 调用时传入**。这与当前 `src/umd/libcuda_shim/cu_module.cpp:76-97` 已有的工作模式**完全一致**：
> - 当前 `cuModuleGetFunction` 接受 app-supplied `const char* name` 入参
> - 存储到 `func_to_name[hfunc] = name`（无 PTXIR 解析）
> - 后续 `cuLaunchKernel` 通过 `resolve_func_name_impl(f)` 拿 name 调 `runtime()->launch_kernel(name, ...)`
>
> **关键事实**：launch 流程**从未**需要 UMD 从 PTXIR 提取 kernel_name —— kernel_name 是**用户提供的字符串标识符**，UMD 仅做 passthrough。
>
> **错误前提澄清**（Oracle 2026-08-20 验证）：
> - ~~"UMD 自解析 PTXIR 违反 ADR-036 UMD 薄原则"~~ → ADR-036 **不包含**"UMD 薄"原则；ADR-036 §Decision line 41 仅规定 `shared` 是 ABI 契约，UMD 不持 PTX-EMU link 依赖
> - ~~"kernel_name 应由 HAL 提供"~~ → 来自已被 Superseded 的 ADR-076 v1 / ADR-0029 §D8.3 时代；当前 v2 §D1.2 显式标注"❌ 不经 HAL"
>
> **方案 A → A′ 降级为可选 MANIFEST 验证**（per Oracle 修订）：
>
> #### 方案 A′（采纳，作为 launch 路径可选增强）：UMD 侧可选 PTXIR 验证
>
> **何时需要**：仅当希望提供 `CUDA_ERROR_NOT_FOUND` 提前错误（实时 CUDA 在 typo kernel_name 时返该错误）。
>
> **实施（按 PTX-EMU v4 真实 24B header + ADR-0028 multi-kernel `kernels[]` 向量）**：
> ```cpp
> // src/umd/libcuda_shim/ptxir_parser.hpp (NEW, 与 §A 共享)
> namespace async_task::umd::shim {
> // 24B header (per ptxir_format.h:53-62)
> #pragma pack(push, 1)
> struct ptixir_header {
>   char     magic[4];              // "PTXI"
>   uint16_t version;               // current = 4
>   uint16_t flags;
>   uint16_t section_count;
>   uint16_t reserved;
>   uint32_t string_table_offset;
>   uint32_t string_table_size;
>   uint32_t header_size;           // = 24
> };
> #pragma pack(pop)
>
> // TOC entry (6B per ptxir_format.h:65-69)
> struct ptixir_toc_entry {
>   uint8_t  type;                  // PtxirSectionType
>   uint8_t  reserved;
>   uint32_t offset;
> };
>
> // MANIFEST section iteration (per ADR-0028 + ptxir_writer.cpp:33-82)
> // - cubin_hash (32B, SHA-256)
> // - kernel_name (NUL-terminated string) - v1 backward-compat
> // - ptx_address_size (u8)
> // - params (count u16 + N × (NUL string + u16 size + u8 kind))
> // - kernels (count u16 + N × (NUL string + u32 arg_count + u32 arg_byte_size))
>
> // 返回 kernel manifest 中所有 kernel 名 (multi-kernel per ADR-0028)
> inline std::vector<std::string>
> ptxir_list_kernel_names(const void* image, uint64_t image_size) {
>   // 1. parse 24B header
>   // 2. walk `section_count` × 6B TOC entries, find type == 6 (MANIFEST)
>   // 3. parse MANIFEST: iterate kernels[] (skip v1 kernel_name compat field)
>   // 4. return vector of kernel names
> }
> }  // namespace async_task::umd::shim
>
> // cu_module.cpp 集成 (cuModuleGetFunction 内，可选):
> // if (cu_module_validation_enabled_) {
> //   auto names = ptxir_list_kernel_names(image_bytes_, image_size_);
> //   if (std::find(names.begin(), names.end(), name) == names.end()) {
> //     return CUDA_ERROR_NOT_FOUND;  // typo kernel_name 提前错误
> //   }
> // }
> ```
>
> **优点**：可选验证，与真 CUDA 语义对齐；提供 `CUDA_ERROR_NOT_FOUND` 错误
> **缺点**：需迭代 TOC + MANIFEST section（~50 行代码）；manifest 格式依赖 PTX-EMU writer 演进
> **决策**：A′ 是 PoC 阶段**可选**（不在 tadr-308 §Decision 1.1 强制要求），tasks.md T-011 §C 标记为 deferred
>
> **方案 B（不采纳）**：通过 ioctl 增加 `kernel_name` OUT 字段
> - 违反 ADR-090 v2 §D1.2（已删除字段）+ ADR-023 append-only（再 churn shipped ABI）
> - Oracle session `ses_fe0443831ffenUxpEQxZqWE8Cp` 明确否决
>
> **方案 C（不采纳）**：通过 `cuModuleGetFunction` lookup 路径（原始方案 B）
> - 与 A′ 等效，但需要先解析 PTXIR 才能 lookup；A′ 更通用（可同时支持 §A size 推断）
>
> **与 PTX-EMU `ptxemu_image_kernel_name` 关系**：
> - TaskRunner UMD 路径**不调** PTX-EMU ABI（per ADR-090 v2 §D3.5，PTX-EMU 仅由 CppTLM Mode B 调用）
> - A′ 是 shim 内部 PTXIR 解析（per ADR-090 v2 §C2 "PTXIR header 解析是 UMD 职责，不是 driver 职责"）
> - 两份解析逻辑独立存在（shim 一份 + CppTLM 一份），格式必须一致（per PTX-EMU ADR-0023 24B header + ADR-0028 kernels[]）

### 1.2. CUmodule 句柄语义重定义（per Oracle 2026-08-20 A′ 修订）

> **CUmodule 重定义为 `code BO GPU VA`**（`uint64_t` reinterpret）——这是 tadr-308 的**核心句柄语义变更**。
>
> **理由**（per Oracle session `ses_fe0443831ffenUxpEQxZqWE8Cp` 验证）：
> 1. `load_kernel_module` 通过 ioctl 0x27 返回 `out_vram_addr`（code BO GPU VA，per ADR-090 v2 §D1 重定义）
> 2. shim 层 `*module = reinterpret_cast<CUmodule>(vram_addr)` 直接用 VA 作为 handle
> 3. 与 `cuModuleLoad`（per T-011 §B）统一走 VRAM-load 路径后，所有 CUmodule 来源都是 GPU VA
> 4. 避免 cu_module.cpp:288-291 (Oracle C1 警告) 命名空间冲突：整数 vs GPU VA 撞号
>
> **关键不变式**：
> - `CUmodule = uint64_t reinterpret of vram_addr`（CUDA Driver API 用户视角仍 opaque handle）
> - `cuModuleGetFunction` 不解释 CUmodule 含义（仅作 map key）
> - `cuLaunchKernel` 通过 `func_to_name[f]` 拿 string，**不依赖 CUmodule 语义**（per `cu_launch.cpp:62-95` 验证）
> - `cuModuleUnload(mod)` 通过 `reinterpret_cast<uint64_t>(mod)` 取回 vram_addr → `free_bo(vram_addr)`（per T-003 G2 扩展）
>
> **影响范围**（per Oracle Part B）：
> - 所有 `g_handles` 表（`mod_to_func` / `mod_to_name`）key 类型 CUmodule 实际是 u64 VA
> - `CUmodule` 重定义为 u64 GPU VA 后，`free_bo` 入参语义需对齐（**G2**：见 T-003）
> - launch 路径**不直接消费** vram_addr（per Oracle G1 缺口，T-013 NEW 解决）
>
> **ABI 审计结论**（per tadr-308 §CUmodule 句柄语义重定义 ABI 审计）：
> - 原断言"无运行时代码消费 CUmodule"**错误**——Oracle 实证发现 `cu_module.cpp:66-74` cuModuleLoad 用递增整数 + `cu_module.cpp:220` cuFuncGetModule 读 func_to_module 反查
> - tadr-308 §Decision 1.2 统一为 GPU VA 命名空间，**消除**冲突
> - 与 tadr-307 时代方案 A（高位 flag 编码）相比更彻底，避免长期维护复杂

### 2. 不删除任何现有方法

**tadr-307 提议的删除清单作废**——tadr-308 **不删除**任何现有 IGpuDriver 方法，**仅追加** 1 个新方法。理由：
- 现有 49 个虚方法已被多个实现者覆盖，删除会破坏向后兼容
- `submit_batch` (`include/shared/igpu_driver.hpp:191`) 已存在（**真实方法名**, 不是 `submit_pushbuffer_batch`——后者是 PTX-EMU owner review 中的笔误）
- launch/unload kernel module 走 `submit_batch` 路径（DISPATCH_KERNEL packet per ADR-090 v2 §D3.3 + tasks.md T-013），不需要新增方法

> **⚠️ Oracle 2026-08-18 第二轮审查 M8 警告（session `ses_feaa41dfaffeVTyUSpzNH5FDx2`）**：
> tadr-308 §Decision 2 与 [ADR-090 v1 §D4](../docs/00_adr/adr-090-ptxir-via-h2d-dma.md#d4-taskrunner-tadr-308-修订需求) 描述**表面矛盾**：
> - ADR-090 v1 §D4：tadr-308 需"删除 3 纯虚方法（#48-50）"
> - tadr-308 §Decision 2："append-only 新增 1 个新方法"
>
> **真相澄清**：tadr-307 提议的 3 个方法（load/launch/unload_kernel_module）**从未 ship**——tadr-307 仅是 PROPOSED 文档（per `tasks.md T-006` "tadr-307 是 PROPOSED 未实施"），仓内 IGpuDriver grep 0 命中 `kernel_module` 方法。**没有代码可删除**。
>
> **跨仓一致性建议（per Oracle 2026-08-20 修订）**：ADR-090 v1 已被 v2 ✅ Accepted 取代，**不**amend v1 Superseded 文档。**已在 tadr-308 §1.5 (Commit 2) + §Consequences (Commit 4) + tasks.md T-010 DROP 替代**——3 处覆盖 v1 §D4 描述真相。

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
    // 新实现 (per tadr-308 + ADR-090 v2 §D3 + tasks.md T-002):
    if (!module || !image) return CUDA_ERROR_INVALID_VALUE;
    uint64_t vram_addr = 0;
    int rc = runtime()->load_kernel_module(image, image_size, &vram_addr);
    if (rc != 0) return cuda_error_from_errno(rc);  // NOT return rc (errno 负值非法 CUDA error)
    *module = reinterpret_cast<CUmodule>(vram_addr);  // 重定义 CUmodule = code BO GPU VA
    return CUDA_SUCCESS;
}
```

`CUmodule` 句柄语义重定义 = code BO GPU VA（需在文件头注释 `:9-14` 区域补充契约说明）。

`cuModuleUnload`（`:99` 区域已有 "not loaded via cuModuleLoad" 分支）补 vram_addr → `GPU_IOCTL_FREE_BO` 路径：

```cpp
CUresult cuModuleUnload(CUmodule module) {
    if (!module) return CUDA_ERROR_INVALID_VALUE;
    uint64_t vram_addr = reinterpret_cast<uint64_t>(module);
    int rc = runtime()->free_bo(vram_addr);
    return cuda_error_from_errno(rc);
}
```

> **⚠️ Oracle M1 警告（2026-08-18）+ Oracle 2026-08-20 F4 缩窄**：
> `cuModuleUnload` 当前实现（`cu_module.cpp:99-114`）**部分**清理 handle 表：
> - ✅ **已清理**：`func_to_name`（per line 107 循环 erase）
> - ❌ **未清理**：`func_to_attrs` + `func_to_module`（导致悬挂 CUfunction handle）
>
> Oracle 2026-08-20 `ses_fe0443831...` F4 实地验证：`mod_to_func` + `mod_to_name` 也已清理，仅 `func_to_attrs`/`func_to_module` 需补充。
>
> **修复草案**（`tasks.md T-005b` 修订，per Oracle F4 缩窄范围）：
> ```cpp
> extern "C" CUresult cuModuleUnload(CUmodule module) {
>   std::lock_guard<std::mutex> lock(g_handles.mu);
>   // M1 + F4 修复: 补充清理 func_to_attrs + func_to_module (func_to_name 已清理)
>   auto it = g_handles.mod_to_func.find(module);
>   if (it != g_handles.mod_to_func.end()) {
>     for (CUfunction func : it->second) {
>       g_handles.func_to_attrs.erase(func);   // F4: 新增
>       g_handles.func_to_module.erase(func);  // F4: 新增
>     }
>     g_handles.mod_to_func.erase(it);
>   }
>   g_handles.mod_to_name.erase(module);
>   // 然后调 free_bo (per T-003 G2: VA → get_bo_gpu_va → BO handle → free_bo)
>   uint64_t vram_addr = reinterpret_cast<uint64_t>(module);
>   uint32_t bo_handle = 0;
>   int rc = runtime()->get_bo_gpu_va(vram_addr, &bo_handle);
>   if (rc != 0) return cuda_error_from_errno(rc);
>   rc = runtime()->free_bo(bo_handle);
>   return cuda_error_from_errno(rc);
> }
> ```
>
> ~~Oracle 建议新增 `IGpuDriver::unload_kernel_module(uint64_t handle)` 默认 -ENOSYS 方法~~ → **per Oracle 2026-08-20 修订**：PoC 阶段选 G2 路径（VA 反查 + free_bo），**不**新增 IGpuDriver 方法（per ADR-023 append-only）。如未来需要更明确 unload 语义，发起独立 sub-change（见 tasks.md T-014 placeholder）。

`cu_launch.cpp` 本轮**不动**（0x28 LAUNCH 已 deprecated，-ENOSYS per ADR-090 v2 §D2.2 — v2 修正 v1 §D2.2 编号漂移）。

> **Oracle M4 注解（2026-08-18）**：`cu_launch.cpp` 当前实现（`runtime()->launch_kernel(name, ...)`）**不读 CUmodule**，
> `CUmodule` 重定义对 launch 路径**无实际效果**。重定义主要为 `cuFuncGetModule` + `cuModuleUnload` 链服务（M1 修复后生效）。
> 若 owner 选择退回 tadr-307 思路（3 方法独立 launch_kernel_module），本注解失效。

### 与既有 49 方法的兼容性

- 新方法 `load_kernel_module` 走 append-only（per ADR-023 §D4）
- 现有 3 个 IGpuDriver 实现者不受影响（默认体 `-ENOSYS` 不破坏现有调用）
- test-fixture (`cuda_stub.hpp`, `gpu_driver_client.h`, `mock_gpu_driver.hpp`) 可逐步实现 `load_kernel_module` 而不强制

### 并发/同步语义

`load_kernel_module` 同步返回（与 tadr-307:102 一致）—— 用户态等待 image 解析完成 + 写入 VRAM 返回。后续 PTX-EMU 实际执行（DISPATCH_KERNEL packet）走 `submit_batch` 异步路径。

### `CUmodule` 句柄语义重定义 ABI 审计

**变更概要**：本 TADR 把 `CUmodule` 句柄从 opaque handle 重定义为 `code BO GPU VA`（uint64_t reinterpret）。这是 ABI-breaking 变更，需审计所有消费 `CUmodule` 的 shim 入口与 IGpuDriver 方法。

**受影响的 shim 入口**（`src/umd/libcuda_shim/`，17 文件）：

| shim 入口 | 接收 `CUmodule`？| 影响 |
|-----------|-----------------|------|
| `cu_module.cpp::cuModuleGetFunction` | ✅ `CUmodule hmod` 入参 | 解析为 GPU VA → 需传 `kernel_index` 给 IGpuDriver.submit_launch；现有 NOT_IMPLEMENTED |
| `cu_module.cpp::cuModuleGetGlobal` | ✅ `CUmodule hmod` | 同上（待实现）|
| `cuLaunchKernel` 内 `func` 参数 | 间接（通过 `func_to_module[f]` 反查）| tadr-308 §Decision 1.3.3 已设计 fast-path |
| 其他 cu_*.cpp | ❌ 不直接接 CUmodule | 无影响 |

**受影响的 IGpuDriver 方法**：

- `submit_batch`（已有，line 191）—— DISPATCH_KERNEL packet 内 `kernel_index` 不变
- `submit_launch`（已有，line 219）—— `kernel_index` 入参，不接 CUmodule

**审计结论（修订版，per Oracle 2026-08-18 session `ses_feb85d969ffe0qPwACwwapfXen`）**：

- ❌ **原断言错误**："无运行时代码消费 `CUmodule`" 是错的。Oracle 实证发现：
  - `cu_module.cpp:66-74` `cuModuleLoad` 路径用**递增整数**作为 `CUmodule`（`next_id.fetch_add(1)`）
  - `cu_module.cpp:220` `cuFuncGetModule` 读 `func_to_module` 反查 `CUmodule` 并返回给上层
  - 若应用混用 `cuModuleLoad`(整数) + `cuModuleLoadData`(GPU VA) + `cuFuncGetModule` + `cuModuleUnload`，**整数 CUmodule 会传给 `free_bo` 错杀用户 BO**
- ✅ **`func_to_module` 反查表**——tadr-308 §Decision 1.3.3 新增字段，键 = `CUfunction`，值 = `CUmodule`，与本次重定义兼容
- ⚠ **`cuModuleGetFunction`/`cuModuleGetGlobal` 实施时**——必须把 `CUmodule` 解释为 GPU VA，配合 `GpuDriverClient::get_bo_gpu_va()` 做反向查询
- 📋 **后续审计任务**（建议加到 tasks.md）：在 T-002/T-003 实施前 `grep -rn "CUmodule" src/umd/libcuda_shim/` 确认无遗漏

> **⚠️ Oracle C1 警告（2026-08-18）：CUmodule 命名空间冲突（ABI 灾难）**
>
> **冲突场景**：
> ```cpp
> // 应用混用两条 cuModule 加载路径（PyTorch/TensorFlow 真实用法）：
> cuModuleLoad(&mod_file, "kernel.cubin");        // mod_file = 0x1 (递增整数)
> cuModuleLoadData(&mod_data, ptx_image);         // mod_data = 0xDEADBEEF0000 (GPU VA)
> cuModuleGetFunction(&fn, mod_file, "k1");       // OK
> cuFuncGetModule(&mod_check, fn);                // 返回 0x1
> cuModuleUnload(mod_check);                       // ⛔ free_bo(0x1) → 错杀用户 alloc_bo()
> ```
>
> **owner 决策点（二选一）**：
>
> **方案 A（推荐）**：让 `cuModuleLoad` 也走 VRAM-load 路径
> - 修改 `cu_module.cpp:66-74`：先读文件到 host memory，再调 `load_kernel_module(image, size, &vram_addr)`
> - 所有 CUmodule 来源统一为 GPU VA
> - 优点：彻底解决命名空间冲突
> - 缺点：需实现文件读取 + 加 `CUDA_ERROR_FILE_NOT_FOUND` 错误码定义
>
> **方案 B（备选）**：handle 编码高位区分
> ```cpp
> // 高位置 1 = VRAM-load 模块，0 = 文件加载模块
> constexpr uint64_t VRAM_MODULE_FLAG = 0x8000000000000000ULL;
> constexpr bool IS_VRAM_MODULE(uint64_t m) {
>   return (m & VRAM_MODULE_FLAG) != 0;
> }
> constexpr uint64_t MODULE_RAW(uint64_t m) {
>   return m & ~VRAM_MODULE_FLAG;
> }
> ```
> - cuModuleUnload 根据 flag 分发到 `free_bo(vram)` 或 `mod_to_name` 清理
> - 优点：改动小
> - 缺点：长期维护复杂，PTX-EMU 返回的 VA 可能恰好有高位 1（极小概率但理论存在）
>
> **✅ owner 决策（2026-08-18 Sisyphus）**：选 **方案 A（统一走 VRAM-load）**。
> - `cuModuleLoad(fname)` 路径也调 `load_kernel_module`
> - 需先读文件到 host memory（`fread` 全量读入）
> - shim 加 `CUDA_ERROR_FILE_NOT_FOUND` 错误码定义
> - 实施任务：[tasks.md T-011 §B](../changes/2026-08-18-tadr-308-igpu-driver-vram-load/tasks.md#t-011-决策点代码实施汇总)

**CUmodule 兼容性边界**（per CUDA Driver API 规范）：

```c
// 用户视角（CUDA 标准）：
CUmodule mod;                    // opaque handle, 用户代码不可解读
cuModuleLoadData(&mod, image);    // mod 现在 = GPU VA (TaskRunner 实现细节)
cuModuleGetFunction(&fn, mod, "kernel");  // mod 必须能反向查到 kernel_index
cuLaunchKernel(fn, ...);         // fn 是 kernel handle, 与 mod 解耦
```

重定义 `CUmodule` 为 GPU VA 是 **TaskRunner 内部实现选择**，对外（CUDA 应用）保持 opaque handle 语义；CUmodule ↔ GPU VA 转换在 shim 层完成。

## Acceptance Gate

| Gate | Owner | 状态 | 前置 |
|---|---|:---:|---|
| #1 UsrLinuxEmu ADR-090 v1 ✅ Accepted | UsrLinuxEmu | ✅ | (commit `e03b5a1` 已 ship) |
| #2 CppTLM maintainer ack | CppTLM | ✅ | ([CppTLM #19](https://github.com/chisuhua/CppTLM/issues/19) ack 2026-08-18) |
| #3 TaskRunner owner ack | TaskRunner | ⏳ | **本 tadr-308** |
| #4 PTX-EMU HSK-6 联发 | PTX-EMU | 🚫 | ([PTX-EMU #12](https://github.com/chisuhua/PTX-EMU/issues/12) closed, 跟踪中) |
| #5 TaskRunner openspec change 创建 | TaskRunner | ⏳ | **配套** |
| #6 新增方法 E2E 测试 | TaskRunner | ⏳ | test-suite 扩展 |

### Oracle 2026-08-18 新增硬前置项（per `ses_feb85d969ffe0qPwACwwapfXen`）

| Gate | 描述 | 来源 | 阻塞状态 |
|---|---|---|:---:|
| **#7 CudaRuntimeApi 扩 `load_kernel_module` 方法** | 当前 `include/umd/cuda_runtime_api.hpp` 仅 5 个方法（无 `load_kernel_module`），shim 调 `runtime()->load_kernel_module` 编译失败。必须先扩 CudaRuntimeApi 接口 | Oracle **C3** | ⏳ HARD |
| **#8 `cuda_error_from_errno` helper 定义** | 当前 `src/umd/libcuda_shim/` 0 命中此函数，T-002/T-003 调用编译失败。必须新建 `cuda_error_map.hpp` | Oracle **C4** | ⏳ HARD |
| **#9 既有测试 `tests/umd/test_cuda_shim.cpp:1037` 预期同步** | 当前测试断言 `cuModuleLoadData == CUDA_ERROR_NOT_IMPLEMENTED`，新代码返回 `CUDA_ERROR_NOT_SUPPORTED`，必须更新预期 | Oracle **C5** | ⏳ HARD |
| **#10 image_size 来源 owner 决策** | CUDA API 不传 size，需 owner 选 PTXIR magic 头扫描（方案1）或强制 `cuModuleLoadDataEx`（方案2）| Oracle **C2** | ⏳ HARD |
| **#11 CUmodule 命名空间冲突 owner 决策** | `cuModuleLoad` 路径用递增整数 vs `cuModuleLoadData` 用 GPU VA 冲突，需 owner 选方案 A（统一走 VRAM-load）或方案 B（handle 高位编码）| Oracle **C1** | ⏳ HARD |
| **#12 `func_to_module` 表清理逻辑** | `cuModuleUnload` 当前实现未清理 `func_to_*` 表，导致悬挂 handle | Oracle **M1** | ⏳ HARD |
| **#13 mutex 位置修订** | `tasks.md T-004` 当前提议在 `IGpuDriver` 加 `load_mutex_` 破坏抽象，必须改为 `GpuDriverClient` 私有成员 | Oracle **M3** | ⏳ HARD |
| **#14 父仓契约核验** | 当前 UsrLinuxEmu 符号链接在 Oracle 沙盒内断裂，ioctl 0x27 args / HAL #66 签名 / ADR-090 v2 / PTX-EMU ADR-0029 §D8 / CppTLM #19 未独立核验。owner apply change 前必须在真实环境核验 | Oracle 报告 §4 | ⏳ HARD |

### Oracle 2026-08-18 第二轮审查新增硬前置项（per `ses_feaa41dfaffeVTyUSpzNH5FDx2`）

| Gate | 描述 | 来源 | 阻塞状态 |
|---|---|---|:---:|
| **#15 ADR-090 v2 canonical 引用确认** | tadr-308 §Context 引用 `adr-090-ptxir-via-h2d-dma-v2.md` — Oracle 2026-08-20 验证 v2 ✅ Accepted 为 canonical，v1 🚫 Superseded by v2 | Oracle **C6 修订** | ✅ 已确认 |
| **#16 `kernel_name` 解析路径 owner 决策** | ioctl 0x27 已删 `kernel_name` 字段（per ADR-090 v1 D2），tadr-308 必须决策 M11 三方案（UMD PTXIR 解析 / lookup / 加 ioctl 字段） | Oracle **M11 重大新发现** | ⏳ HARD |
| **#17 `MAX_KERNEL_IMAGE_SIZE` (64MB) 边界处理** | ioctl 0.27 handler 强制 `image_size ∈ [1, 64MB]`，UMD 侧默认体需校验，超限返回 -EINVAL 而非让 ioctl 失败 | Oracle **M10** | ⏳ HARD |
| **#18 HAL #66 vs ioctl 0.27 引用准确化** | tadr-308 §Decision 1.1 引用"HAL #66"误导（实际是 void*args 单参数），应改为 "ioctl 0x27 struct `gpu_load_kernel_module_args`" | Oracle **M6** | ⏳ HARD |
| **#19 PTX-EMU Image Executor 关系澄清** | tadr-308 §Decision 1.1 应明确"不调 PTX-EMU 8 个 ABI（per ADR-090 v1 D3）"，避免读者误解路径依赖 | Oracle **M7** | ⏳ HARD |
| **#20 `image_size` 字段类型对齐** | 已修订 `size_t → uint64_t` 对齐 ioctl 0x27 真实 `u64` | Oracle **C7** | ✅ 已修订 |
| **#21 ADR-090 v1 §D4 矛盾解决** | ADR-090 v1 §D4 描述"删除 3 纯虚方法"与 tadr-308 §Decision 2 "append-only" 表面矛盾。**per Oracle 2026-08-20 修订**：不 amend v1 Superseded 文档；改在 tadr-308 §1.5/§Consequences + tasks.md T-010 DROP 替代（3 处覆盖真相） | Oracle **M8 修订** | ✅ 已解决 |
| **#22 CUfunction 独立计数器（D6 owner 决策）** | `cuModuleGetFunction` (cu_module.cpp:82) 与原 `cuModuleLoad`（已废弃）共享 `g_handles.next_id`。tadr-308 §Decision 1.2 改 `cuModuleLoad` 为 VRAM-load 后，`cuModuleGetFunction` **必须**同步改为独立 `next_func_id` 计数器，避免函数 ID 与模块 ID (GPU VA) 撞号。**owner 已决策 (2026-08-18 Sisyphus)**：选项 A（独立计数器，与仓内 `cu_array`/`cu_event`/`cu_stream` 惯例一致）| Oracle **MF-4 / D6** | ✅ 已决策 (选项 A) |
| **#23 DISPATCH_KERNEL packet 携带 vram_addr (G1)** | `cuLaunchKernel` 路径**不消费** vram_addr（per Oracle G1 重大发现）。修复：在 `submit_batch` (igpu_driver.hpp:191) 扩展 `GPU_OP_DISPATCH_KERNEL = 0x04` packet payload = `{vram_addr(u64), kernel_name(str), grid/block/args/smem}`。**owner 决策**：在 tasks.md T-013 实施 | Oracle **G1** | 📋 待 T-013 实施 |
| **#24 PTX-EMU 真实 fixture 验证（T-009b）** | 用 PTX-EMU `tests/ptxir/fixtures/multi_kernel_basic.ptxir` + `cute_rmsnorm.ptxir` 验证 24B header 格式 + string_table tail 推断 + MANIFEST parse | Oracle **A′ T-009b** | 📋 待 T-009b 实施 |

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
| UsrLinuxEmu | [ADR-090 v2](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/00_adr/adr-090-ptxir-via-h2d-dma-v2.md) (canonical ✅ Accepted) + [annex §E](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/05-advanced/adr-090-cross-repo-coordination.md) | ✅ Accepted (commit `e03b5a1` + `37a91b6`); v1 🚫 Superseded |
| CppTLM | [issue #19](https://github.com/chisuhua/CppTLM/issues/19) v3.0 RFC | ✅ Gate #2 ack 2026-08-18 |
| PTX-EMU | [ADR-0029 §D8](https://github.com/chisuhua/PTX-EMU/blob/main/docs/adr/ADR-0029-ptxemu-image-executor.md#d8-cp-端集成约定--hal-扩展方案usrlinuxemu--ptx-emu-跨仓契约) amendment | 🚫 **per ADR-090 v2 §C4**: §D8 仍描述旧 HAL 方案，待 PTX-EMU owner 发起 amendment (D8.1~D8.8 + D8-Alt); [PTX-EMU #12](https://github.com/chisuhua/PTX-EMU/issues/12) closed |
| TaskRunner | tadr-308 (本文件) + openspec change `2026-08-18-tadr-308-igpu-driver-vram-load` | 📋 本 change 已 Apply 待 owner 启动 (per Oracle A′ 修订 2026-08-20 + 5 个 atomic commit 已落) |

## References

- UsrLinuxEmu [ADR-090 v2](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/00_adr/adr-090-ptxir-via-h2d-dma-v2.md) (**canonical** ✅ Accepted; v1 🚫 Superseded by v2)
- UsrLinuxEmu [ADR-090 v1](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/00_adr/adr-090-ptxir-via-h2d-dma.md) (历史参考，🚫 Superseded)
- UsrLinuxEmu [ADR-036 three-way-separation](../../../../docs/00_adr/adr-036-three-way-separation.md) ✅ Accepted
- UsrLinuxEmu [ADR-023 HAL interface](../../../../docs/00_adr/adr-023-hal-interface.md) ✅ Accepted (HAL append-only 治理)
- UsrLinuxEmu [ADR-035 governance-policy](../../../../docs/00_adr/adr-035-governance-policy.md) ✅ Accepted (跨仓 R5.1 4 步)
- PTX-EMU [ADR-0023 ptxir-binary-format](../../../../../PTX-EMU/docs/adr/ADR-0023-ptxir-binary-format.md) ✅ Accepted (24B header + TOC + Extend-Only 版本管理)
- PTX-EMU [ADR-0028 multi-kernel-manifest](../../../../../PTX-EMU/docs/adr/ADR-0028-multi-kernel-manifest.md) ✅ Accepted (kernels[] 向量 + backward-compat)
- PTX-EMU [ADR-0029 ptxemu-image-executor](../../../../../PTX-EMU/docs/adr/ADR-0029-ptxemu-image-executor.md) ✅ Accepted (cpptlm_module.h 8 ABI; **§D8 待 amendment per ADR-090 v2 §C4**)
- PTX-EMU [`include/ptx_ir/ptxir_format.h`](../../../../../PTX-EMU/include/ptx_ir/ptxir_format.h) PTXIR 24B header + TOC + section 真实格式定义
- UsrLinuxEmu [annex §E 跟踪表](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/05-advanced/adr-090-cross-repo-coordination.md) (`Gate #2 ✅`)
- Oracle session `ses_fef78854dffeLfDJh7p8ELuMLy` (v2 决策 + 4 轮评估)
- Oracle session `ses_ff2106f84ffeM2oItBEa9iu4hL` (v1 启动，识别 ADR-076 v1 违规)
- Oracle session `ses_feb85d969ffe0qPwACwwapfXen` (**2026-08-18 深度审查**：识别 5 CRITICAL + 5 MAJOR + 4 MINOR 问题，含 CUmodule 命名空间冲突 C1、image_size 来源缺失 C2、CudaRuntimeApi 缺方法 C3、cuda_error_from_errno 不存在 C4、测试预期被破坏 C5、func_to_module 清理缺失 M1、IGpuDriver 加 mutex 违反抽象 M3)
- Oracle session `ses_feaa41dfaffeVTyUSpzNH5FDx2` (**2026-08-18 第二轮调研 + 审查**：Phase 1 外部调研 CUDA Driver API + PTX-EMU `cpptlm_module.h` v2 + UsrLinuxEmu `gpu_ioctl.h`/`gpu_hal.h` 真实契约；Phase 2 新增 2 CRITICAL + 6 MAJOR + 3 MINIOR，含 v2 404 假断言 C6 [Oracle 2026-08-20 验证 v2 存在且 canonical, 反向]、`image_size` 类型不匹配 C7、HAL #66 void*args 单参数引用不准确 M6、PTX-EMU 不参与说明缺失 M7、ADR-090 v1 §D4 矛盾 M8、MAX_KERNEL_IMAGE_SIZE 边界 M10、`kernel_name` 字段缺失重大新发现 M11)
- Oracle session `ses_fe0443831ffenUxpEQxZqWE8Cp` (**2026-08-20 终极验证 + A′ 决策**)：验证 (1) v2 存在 + canonical + v1 Superseded；(2) PTX-EMU 24B header + TOC + MANIFEST 真实格式；(3) shipped `gpu_ioctl.h` 字段；(4) `hal_user.cpp:692` 已实现 H2D DMA + 零 ptxemu 符号；(5) GpuDriverClient inline `.h` not `.cpp`；(6) CUmodule=VA redefinition 必要性；建议 A′ (kernel_name app-supplied, image_size 24B 推断, §A 用 string_table tail, §C 降级可选, G1 DISPATCH_KERNEL packet, G2 free_bo VA 翻译)
- CppTLM [issue #19](https://github.com/chisuhua/CppTLM/issues/19) (Gate #2 ack)
- PTX-EMU [issue #12](https://github.com/chisuhua/PTX-EMU/issues/12) (Gate #3 跟踪, closed)
- TaskRunner [issue #10](https://github.com/chisuhua/TaskRunner/issues/10) (Gate #4 跟踪, closed)
- TaskRunner `include/shared/igpu_driver.hpp:191` (`submit_batch` 真实方法名, 不是 `submit_pushbuffer_batch`)
- TaskRunner `src/umd/libcuda_shim/cu_module.cpp:135` (cuModuleLoadData 适配点)
- TaskRunner `src/umd/libcuda_shim/cu_module.cpp:93` (cuModuleUnload 分支点)
- UsrLinuxEmu `plugins/gpu_driver/shared/gpu_ioctl.h:723-779` (0x27 GPU_IOCTL_LOAD_KERNEL_MODULE 契约)
- UsrLinuxEmu `plugins/gpu_driver/hal/gpu_hal.h:370` (HAL #66 kernel_module_load)