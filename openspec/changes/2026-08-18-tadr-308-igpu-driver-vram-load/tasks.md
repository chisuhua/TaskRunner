# tadr-308: Tasks (TDD 5-Step Structure)

> **结构**: 每个 task 标 TDD 5 步 (Write failing test → Verify fail → Implement → Verify pass → Commit)
> **关联**: [proposal.md](proposal.md) · [tadr-308](../shared/adr/tadr-308-igpu-driver-vram-load.md)

---

## ⚠️ Oracle 2026-08-18 Hard Prerequisites (per `ses_feb85d969ffe0qPwACwwapfXen`)

**T-001 ~ T-008 实施前必须先完成以下 3 项**：

### T-000a: CudaRuntimeApi 扩 `load_kernel_module` 方法（C3）

**目的**：当前 `include/umd/cuda_runtime_api.hpp` 仅 5 个方法（register_kernel, malloc, memcpy, launch_kernel, get_total_memory），无 `load_kernel_module`。shim 调 `runtime()->load_kernel_module(...)` 会编译失败。

**Implement**（`include/umd/cuda_runtime_api.hpp` + `src/umd/cuda_runtime_api.cpp`）:
```cpp
// include/umd/cuda_runtime_api.hpp (追加)
class CudaRuntimeApi {
 public:
  // ... 现有 5 个方法 ...

  /**
   * @brief 加载 PTXIR image 到 CppTLM VRAM (H2D DMA 路径)
   * @param image        PTXIR image bytes
   * @param image_size   image byte 数
   * @param out_vram_addr OUT: code BO 的 GPU VA
   * @return CudaError::Success / InvalidValue / NotSupported / OutOfMemory
   */
  CudaError load_kernel_module(const void* image, std::size_t image_size,
                               std::uint64_t* out_vram_addr);
};

// src/umd/cuda_runtime_api.cpp (追加)
CudaError CudaRuntimeApi::load_kernel_module(const void* image,
                                              std::size_t image_size,
                                              std::uint64_t* out_vram_addr) {
  if (!image || !out_vram_addr || image_size == 0) {
    return CudaError::InvalidValue;
  }
  int rc = scheduler_->driver()->load_kernel_module(
      image, image_size, out_vram_addr);
  if (rc == 0) return CudaError::Success;
  if (rc == -ENOSYS) return CudaError::NotSupported;
  if (rc == -ENOMEM) return CudaError::OutOfMemory;
  return CudaError::Unknown;
}
```

**Commit**: `feat(runtime-api): append load_kernel_module to CudaRuntimeApi (tadr-308 T-000a)`

### T-000b: `cuda_error_from_errno` helper 定义（C4）

**目的**：`src/umd/libcuda_shim/` 0 命中此函数，T-002/T-003 调用编译失败。

**Implement**（新建 `src/umd/libcuda_shim/cuda_error_map.hpp`）:
```cpp
#pragma once
#include <cuda.h>
#include <cerrno>

namespace async_task::umd::shim {

inline CUresult cuda_error_from_errno(int rc) {
  if (rc == 0) return CUDA_SUCCESS;
  switch (rc) {
    case -EINVAL:    return CUDA_ERROR_INVALID_VALUE;
    case -EFAULT:    return CUDA_ERROR_INVALID_VALUE;
    case -ENOMEM:    return CUDA_ERROR_OUT_OF_MEMORY;
    case -ENOSYS:    return CUDA_ERROR_NOT_SUPPORTED;
    case -EBUSY:     return CUDA_ERROR_INVALID_HANDLE;
    case -ETIMEDOUT: return CUDA_ERROR_TIMEOUT;
    default:         return CUDA_ERROR_UNKNOWN;
  }
}

}  // namespace async_task::umd::shim
```

**同步更新**：`src/umd/libcuda_shim/cu_module.cpp` 顶部加 `#include "cuda_error_map.hpp"`

**Commit**: `feat(shim): add cuda_error_from_errno helper (tadr-308 T-000b)`

### T-000c: image_size 来源 owner 决策（C2）— **DECIDED per Oracle A′ 2026-08-20**

**目的**：CUDA API `cuModuleLoadData` 无 size 参数，T-002 实施前需 owner 选方案 1（PTXIR 24B header 扫描）或方案 2（强制 `cuModuleLoadDataEx`）。

**Action**：
1. ~~在 [tadr-308 §Decision 1.3.1](../shared/adr/tadr-308-igpu-driver-vram-load.md) C2 警告段标注 owner 决策~~ → **已在 Commit 2 (83abae0) 落地**：选择方案 1（PTXIR 24B header + string_table tail 推断）
2. ~~在 [issues/10](https://github.com/chisuhua/TaskRunner/issues/10) 提请 owner 决策~~ → **已落地**（commit 9431cce 反映）
3. ~~owner 决策前 T-002 **不实施**~~ → **解除阻塞**（per Commit 2 owner 决策 + Oracle A′ 验证 2026-08-20）

**Commit**: ✅ `docs(tadr-308): apply Oracle A' decision (commit 83abae0)` — 已合并到 Commit 2

**实施位置**（per tadr-308 §A + tasks.md T-011 §A）：`src/umd/libcuda_shim/ptxir_parser.hpp` 提供 `ptxir_total_size()` helper，T-002 在 `cuModuleLoadData` 调用前用其推断 size。

---

## T-001: `IGpuDriver::load_kernel_module` 默认实现测试 (TDD 5 步)

**Write failing test**:
```cpp
// tests/test_load_kernel_module_standalone.cpp
TEST_CASE("IGpuDriver::load_kernel_module default impl returns -ENOSYS", "[tadr-308][T-001]") {
    auto* drv = IGpuDriver::create_mock();  //  默认 mock 实现
    uint64_t vram_addr = 0;
    int rc = drv->load_kernel_module(nullptr, 0, &vram_addr);
    REQUIRE(rc == -ENOSYS);
    REQUIRE(vram_addr == 0);  // 默认实现不修改 OUT 参数
}
```

**Verify fail**: `cmake --build` 应该 link 失败（`load_kernel_module` 未声明）。捕获错误信息。

**Implement**:
```cpp
// include/shared/igpu_driver.hpp
virtual int load_kernel_module(const void* image, uint64_t image_size,
                               uint64_t* out_vram_addr) {
    (void)image; (void)image_size; (void)out_vram_addr;
    return -ENOSYS;
}
```

**Verify pass**: `ctest -R tadr-308` PASS。

**Commit**: `feat(igpu-driver): append load_kernel_module default impl (tadr-308 T-001)`

---

## T-002: `cuModuleLoadData` 接入 `load_kernel_module` 适配 (TDD 5 步)

**Write failing test**:
```cpp
TEST_CASE("cuModuleLoadData forwards to load_kernel_module", "[tadr-308][T-002]") {
    CUmodule module = nullptr;
    const uint8_t fake_image[] = {0xDE, 0xAD, 0xBE, 0xEF};
    CUresult rc = cuModuleLoadData(&module, fake_image);
    REQUIRE(rc == CUDA_SUCCESS);
    REQUIRE(module != nullptr);  // 应该是 vram_addr 句柄
    // 验证 mock 收到 image 数据
    REQUIRE(mock_drv()->last_image_size() == 4);
}
```

**Verify fail**: `cuModuleLoadData` 当前返回 `CUDA_ERROR_NOT_IMPLEMENTED`。测试捕获该错误。

**Implement**:
```cpp
// src/umd/libcuda_shim/cu_module.cpp:135
CUresult cuModuleLoadData(CUmodule* module, const void* image) {
    if (!module || !image) return CUDA_ERROR_INVALID_VALUE;

    // C2 修订: image_size 来源 — 取决于 owner 决策 T-000c
    // 方案 1 (PTXIR magic 头扫描): size_t image_size = ptixir_total_size(image);
    // 方案 2 (强制 LoadDataEx):     return CUDA_ERROR_INVALID_VALUE;
    size_t image_size = 0;  // 占位 — T-000c 决策后填充 (实际 type 与 image_size_t 一致)
    if (image_size == 0) return CUDA_ERROR_INVALID_VALUE;

    uint64_t vram_addr = 0;
    int rc = runtime()->load_kernel_module(image, image_size, &vram_addr);
    if (rc != 0) return cuda_error_from_errno(rc);
    *module = reinterpret_cast<CUmodule>(vram_addr);
    return CUDA_SUCCESS;
}
```

> **修正点（per 2026-08-18 Oracle review）**：
> 1. 加 nullptr 参数检查
> 2. 用 `cuda_error_from_errno(rc)` 而非 `static_cast<CUresult>(rc)`
> 3. **C2 image_size 来源待 T-000c owner 决策**（当前占位）

**Verify pass**: `ctest -R tadr-308` PASS。

**Commit**: `feat(cu_module): forward cuModuleLoadData to load_kernel_module (tadr-308 T-002)`

---

## T-003: `cuModuleUnload` 接入 `free_bo` 路径 (TDD 5 步) — G2 VA 翻译扩展

**Write failing test**:
```cpp
TEST_CASE("cuModuleUnload forwards to free_bo via VA translation (G2)", "[tadr-308][T-003]") {
    // 模拟 ioctl 0x27 返回 vram_addr
    CUmodule module = (CUmodule)0xDEADBEEFCAFEBABEULL;
    CUresult rc = cuModuleUnload(module);
    REQUIRE(rc == CUDA_SUCCESS);
    // G2 修订: free_bo 接受 BO handle (u32), CUmodule 是 GPU VA (u64)
    // 需通过 get_bo_gpu_va(vram_addr) 反查 → u32 handle → free_bo(handle)
    REQUIRE(mock_drv()->last_freed_bo_handle() == /*translated*/);
}
```

**Verify fail**: `cuModuleUnload` 当前返回 NOT_IMPLEMENTED。

**Implement** (G2 修订版):
```cpp
// src/umd/libcuda_shim/cu_module.cpp:99 (cuModuleUnload 当前实现位置)
CUresult cuModuleUnload(CUmodule module) {
    if (!module) return CUDA_ERROR_INVALID_VALUE;

    std::lock_guard<std::mutex> lock(g_handles.mu);

    // T-005b: 清理 func_to_attrs/func_to_module 表
    auto it = g_handles.mod_to_func.find(module);
    if (it != g_handles.mod_to_func.end()) {
        for (CUfunction func : it->second) {
            g_handles.func_to_name.erase(func);
            g_handles.func_to_attrs.erase(func);  // MF-2 修订: T-005b 已加
            g_handles.func_to_module.erase(func);
        }
        g_handles.mod_to_func.erase(it);
    }
    g_handles.mod_to_name.erase(module);

    // G2: CUmodule 是 u64 GPU VA → 通过 get_bo_gpu_va 反查 BO handle → free_bo(handle)
    uint64_t vram_addr = reinterpret_cast<uint64_t>(module);
    uint32_t bo_handle = 0;
    int rc = runtime()->get_bo_gpu_va(vram_addr, &bo_handle);  // 反查
    if (rc != 0) return cuda_error_from_errno(rc);

    rc = runtime()->free_bo(bo_handle);  // free_bo 接 u32 handle
    return cuda_error_from_errno(rc);
}
```

> **G2 修订（per Oracle session `ses_fe0443831ffenUxpEQxZqWE8Cp` Part B G2）**：
> 1. `free_bo` 当前签名接 u32 BO handle（`gpu_driver_client.h:256-258`）
> 2. CUmodule 重定义为 u64 GPU VA 后，类型不匹配
> 3. 解决：调 `get_bo_gpu_va(vram_addr, &handle)` 反查 BO handle，再调 `free_bo(handle)`
> 4. 备选方案：直接调 ioctl 0x29 `GPU_IOCTL_UNLOAD_KERNEL_MODULE`（`module_handle = vram_addr` 语义，per ADR-090 v2 §D1.2 表保留）
> 5. 选择反查路径因为：(a) 不新增 ioctl 0x29 调用链；(b) `get_bo_gpu_va` 已存在 (`igpu_driver.hpp:162`)
>
> **修正点（per 2026-08-18 review）**：
> 1. 加 `if (!module)` nullptr 检查（之前缺失）
> 2. 用 `cuda_error_from_errno(rc)` 而非 `static_cast<CUresult>(rc)`——`free_bo` 返回负 errno（如 -EINVAL/-ENOMEM），CUresult 枚举仅 0-999 合法，强转会产生非法 CUDA error code
> 3. 行号修正：`:93` → `:99`（实测 `cuModuleUnload` 在 cu_module.cpp:99）

**Verify pass**: `ctest -R tadr-308` PASS。

**Commit**: `feat(cu_module): forward cuModuleUnload to free_bo via VA translation G2 (tadr-308 T-003)`

---

## T-004: `GpuDriverClient::load_kernel_module` 并发安全（M3 修订版）

> **Oracle M3 修订（2026-08-18）**：原 T-004 提议在 `IGpuDriver` interface 加 `load_mutex_` 成员，**违反 interface 抽象纯洁性**。
> 正确做法：mutex 加在具体实现类 `GpuDriverClient` 内，mock 不加锁（header-only 单测串行）。

**Write failing test**:
```cpp
TEST_CASE("GpuDriverClient::load_kernel_module is thread-safe", "[tadr-308][T-004]") {
    GpuDriverClient client;
    // ... open device + 构造 N=8 并发线程 ...
    const int N = 8;
    std::vector<std::thread> threads;
    std::atomic<int> ok_count{0};
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&]() {
            uint64_t vram_addr = 0;
            int rc = client.load_kernel_module(nullptr, 0, &vram_addr);
            if (rc == -ENOSYS || rc == -EINVAL || rc == 0) ok_count++;
        });
    }
    for (auto& t : threads) t.join();
    REQUIRE(ok_count == N);
}
```

**Verify fail**: 并发调用未加锁时会偶发失败（如 ioctl fd 状态错乱）。

**Implement**:
```cpp
// include/test_fixture/gpu_driver_client.h (修订)
class GpuDriverClient : public IGpuDriver {
 private:
  // ... 既有成员 ...
  std::mutex load_mutex_;  // M3 修复: mutex 仅在 client 内
 public:
  // ... 既有方法 ...

  int load_kernel_module(const void* image, size_t image_size,
                          uint64_t* out_vram_addr) override {
    std::lock_guard<std::mutex> lock(load_mutex_);  // 仅 client 加锁
    if (!is_open()) return -ENODEV;
    if (!image || !out_vram_addr || image_size == 0) return -EINVAL;
    // ... 调 ioctl ...
  }
};

// tests/test_fixture/mock_gpu_driver.hpp (不变)
// mock 不加锁 - header-only mock, 单测串行调用
int load_kernel_module(const void* image, size_t image_size,
                        uint64_t* out_vram_addr) override {
  if (out_vram_addr) *out_vram_addr = 0xCAFE0001;  // mock handle
  return 0;
}
```

**Verify pass**: `ctest -R tadr-308` PASS（多线程稳定）。

**Commit**: `feat(gpu-driver-client): thread-safe load_kernel_module mutex (tadr-308 T-004 M3 修订)`

---

## T-005: 错误注入测试（M3 修订版）

> **Oracle M3 修订（2026-08-18）**：参数校验逻辑放 IGpuDriver 默认体（覆盖 mock + client），mutex 仍在 GpuDriverClient 内。

**Write failing test**:
```cpp
TEST_CASE("load_kernel_module error paths", "[tadr-308][T-005]") {
    auto* drv = IGpuDriver::create_mock();
    uint64_t vram_addr = 0xDEADBEEF;  // sentinel

    SECTION("image == NULL") {
        int rc = drv->load_kernel_module(nullptr, 1024, &vram_addr);
        REQUIRE(rc == -EINVAL);
        REQUIRE(vram_addr == 0xDEADBEEF);  // 未修改
    }
    SECTION("image_size == 0") {
        int rc = drv->load_kernel_module("x", 0, &vram_addr);
        REQUIRE(rc == -EINVAL);
    }
    SECTION("out_vram_addr == NULL") {
        int rc = drv->load_kernel_module("x", 1024, nullptr);
        REQUIRE(rc == -EFAULT);
    }
}
```

**Verify fail**: 当前默认体只返回 -ENOSYS，不验证 NULL 参数。测试捕获非 -EINVAL 返回。

**Implement**:
```cpp
// include/shared/igpu_driver.hpp (默认体，参数校验 + 无 mutex)
virtual int load_kernel_module(const void* image, uint64_t image_size,
                               uint64_t* out_vram_addr) {
    if (!out_vram_addr) return -EFAULT;
    if (!image && image_size > 0) return -EINVAL;
    if (image_size == 0) return -EINVAL;
    return -ENOSYS;
}

// include/test_fixture/gpu_driver_client.h (override, 加 mutex)
int load_kernel_module(...) override {
    std::lock_guard<std::mutex> lock(load_mutex_);  // M3 修订: mutex 仅 client
    if (!is_open()) return -ENODEV;
    // 默认体已做参数校验, 这里只需业务逻辑
    gpu_load_kernel_module_args args{...};
    int rc = ioctl(fd_, GPU_IOCTL_LOAD_KERNEL_MODULE, &args);
    if (rc < 0) return -errno;
    *out_vram_addr = args.out_module_handle;
    return 0;
}
```

**Verify pass**: `ctest -R tadr-308` PASS。

**Commit**: `feat(igpu-driver): validate load_kernel_module args in default impl (T-005)`

---

## T-005b: `cuModuleUnload` 清理 `func_to_module` 表（M1 修复）

> **Oracle M1 警告（2026-08-18）**：`cuModuleUnload` 当前实现**未清理** `func_to_module` / `mod_to_func` / `func_to_name` / `func_to_attrs` 表，
> 导致已卸载 module 的 CUfunction 仍可被 `cuLaunchKernel` 调用（悬挂 handle）。

**Write failing test**:
```cpp
TEST_CASE("cuModuleUnload cleans up func_to_* tables", "[tadr-308][T-005b]") {
    CUmodule mod;
    const uint8_t image[] = {0x50, 0x54, 0x49, 0x52};  // PTXIR magic
    cuModuleLoadData(&mod, image);
    CUfunction fn;
    cuModuleGetFunction(&fn, mod, "kernel");
    REQUIRE(fn != nullptr);

    cuModuleUnload(mod);

    // 验证 handle 表已清理
    CUfunction fn_after;
    CUresult rc = cuModuleGetFunction(&fn_after, mod, "kernel");
    REQUIRE(rc == CUDA_ERROR_INVALID_HANDLE);  // 不能再次 get function
}
```

**Verify fail**: 当前 `cuModuleUnload` 不清理表，`cuModuleGetFunction` 仍能成功。

**Implement**:
```cpp
// src/umd/libcuda_shim/cu_module.cpp:99 (cuModuleUnload 修订)
extern "C" CUresult cuModuleUnload(CUmodule module) {
    std::lock_guard<std::mutex> lock(g_handles.mu);

    // M1 修复: 清理所有 func_to_* 表
    auto it = g_handles.mod_to_func.find(module);
    if (it != g_handles.mod_to_func.end()) {
        for (CUfunction func : it->second) {
            g_handles.func_to_name.erase(func);
            g_handles.func_to_attrs.erase(func);
            g_handles.func_to_module.erase(func);
        }
        g_handles.mod_to_func.erase(it);
    }
    g_handles.mod_to_name.erase(module);

    // 调 free_bo 释放 VRAM
    uint64_t vram_addr = reinterpret_cast<uint64_t>(module);
    int rc = runtime()->free_bo(vram_addr);
    return cuda_error_from_errno(rc);
}
```

**Verify pass**: `ctest -R tadr-308` PASS。

**Commit**: `fix(cu_module): clean up func_to_* tables in cuModuleUnload (tadr-308 T-005b)`

---

## T-006: tadr-307 标 STALE (无 TDD, 文档任务)

**Action**:
1. 编辑 `docs/shared/adr/tadr-307-igpu-driver-kernel-module-extension.md`
2. 在 frontmatter 顶部插入 STALE 块
3. 不改 tadr-307 任何现有内容

**Commit**: `docs(tadr-307): mark STALE per tadr-308 supersession (2026-08-18)`

---

## T-007: openspec change 自身归档 (Phase 2)

**Action**:
1. 等所有测试 PASS
2. 等 PTX-EMU HSK-6 ACCEPTED + CppTLM P0-1 完成 + TaskRunner owner ack
3. `git mv openspec/changes/2026-08-18-tadr-308-... openspec/changes/archive/2026-08-XX-tadr-308-...`
4. 更新 `openspec/changes/INDEX.md` 移除活跃段 + 加归档段

**Commit**: `docs(openspec): archive tadr-308 change after Gates #3/#5/#6 ✅`

---

## T-008: 补 `func_to_attrs`/`func_to_module` 映射测试（MF-2 修订）

> **Oracle MF-2 修订（2026-08-19 Oracle `ses_fe13e43d2ffexPXVzuMj7hcyHK`）**：原 T-008 提议 NOT_IMPLEMENTED→NOT_SUPPORTED 错误码翻转，但 `include/cuda.h:67` 已将 `CUDA_ERROR_NOT_IMPLEMENTED` 定义为 `CUDA_ERROR_NOT_SUPPORTED` 的宏别名（同值 801），翻转无实际效果。T-008 改为补 `func_to_attrs`/`func_to_module` 表清理测试。
>
> 断言字面量顺带对齐 `NOT_SUPPORTED` 并注释别名（可读性提升）。

**Write failing test**:
```cpp
// tests/umd/test_cuda_shim.cpp (补充 M1 表清理测试 — 验证 T-005b 后 func_to_* 表完全清理)
TEST_CASE("cuModuleUnload cleans up func_to_attrs AND func_to_module (MF-2)", "[tadr-308][T-008]") {
  CUmodule mod;
  const uint8_t image[] = {0x50, 0x54, 0x49, 0x52};  // PTXIR magic
  REQUIRE(cuModuleLoadData(&mod, image) == CUDA_SUCCESS);
  CUfunction fn;
  REQUIRE(cuModuleGetFunction(&fn, mod, "kernel") == CUDA_SUCCESS);

  // 验证表已填充 (before unload)
  REQUIRE(cuFuncGetModule(&mod, fn) == CUDA_SUCCESS);

  REQUIRE(cuModuleUnload(mod) == CUDA_SUCCESS);

  // 验证表已完全清理 (M1 + MF-2: func_to_name, func_to_attrs, func_to_module, mod_to_func)
  CHECK(cuModuleGetFunction(&fn, mod, "kernel") == CUDA_ERROR_INVALID_HANDLE);
  CHECK(cuFuncGetModule(&mod, fn) == CUDA_ERROR_INVALID_HANDLE);  // MF-2: func_to_module 必须清理
}

// tests/umd/test_cuda_shim.cpp:1035-1041 (字面量对齐 + 注释别名)
TEST_CASE("STUB APIs post tadr-308", "[tadr-308][T-008]") {
  CUmodule mod;
  // tadr-308: cuModuleLoadData 走 load_kernel_module 默认 -ENOSYS
  // CUDA_ERROR_NOT_IMPLEMENTED == CUDA_ERROR_NOT_SUPPORTED (include/cuda.h:67 宏别名)
  CHECK(cuModuleLoadData(&mod, nullptr) == CUDA_ERROR_NOT_SUPPORTED);  // 等价 NOT_IMPLEMENTED
  // 其他仍 NOT_IMPLEMENTED (未走新路径)
  CHECK(cuModuleLoadDataEx(&mod, nullptr, 0, nullptr, nullptr) ==
        CUDA_ERROR_NOT_IMPLEMENTED);
  CHECK(cuModuleLoadFatBinary(&mod, nullptr) == CUDA_ERROR_NOT_IMPLEMENTED);
}
```

**Verify fail**: 当前 `cuModuleUnload` 仅清理 `func_to_name` + `mod_to_func` + `mod_to_name`，未清理 `func_to_attrs` 和 `func_to_module`（per Oracle MF-2 / M1）。`cuFuncGetModule` 在 unload 后仍能返回旧 mod handle（悬挂 handle），触发测试失败。

**Implement**:
- T-005b 已负责 `func_to_attrs`/`func_to_module` 清理
- 本 T-008 仅补测试覆盖 + 注释字面量别名

**Verify pass**: `ctest -R test_cuda_shim` PASS。

**Commit**: `test(cuda_shim): cover func_to_attrs/func_to_module cleanup (tadr-308 T-008 MF-2)`

---

## T-009: 父仓契约核验（Gate #14）

> **Oracle 第二轮审查硬前置**（per `ses_feaa41dfaffeVTyUSpzNH5FDx2`）：Oracle 沙盒内 UsrLinuxEmu 符号链接断裂，ioctl 0x27/HAL #66/ADR-090 v2/PTX-EMU ABI/CppTLM ABI 未独立核验。

**Action**（owner apply change 前必做）：

1. **拉取 UsrLinuxEmu 仓**：`git clone https://github.com/chisuhua/UsrLinuxEmu` 后核对
   - `plugins/gpu_driver/shared/gpu_ioctl.h:740-750` ioctl 0x27 struct 字段（**注意**：v2 §D2.1 doc drift，**以 shipped header 为准**）
   - `plugins/gpu_driver/hal/gpu_hal.h:370` HAL #66 签名（`int (*kernel_module_load)(void *ctx, void *args)`）
   - `plugins/gpu_driver/hal/hal_user.cpp:692` `user_kernel_module_load` 实现（H2D DMA write，**零 ptxemu 符号**）
   - `docs/00_adr/adr-090-ptxir-via-h2d-dma-v2.md` ADR-090 v2（canonical ✅ Accepted）§D1 §D2 §D3
   - `docs/00_adr/adr-090-ptxir-via-h2d-dma.md` ADR-090 v1（🚫 Superseded by v2）仅做历史参考

2. **拉取 PTX-EMU 仓**：`git clone https://github.com/chisuhua/PTX-EMU` 后核对
   - `include/cudart/cpptlm_module.h:12-52` 8 个 ABI 函数签名（CPPTLM_MODULE_VERSION 1 — 修正 Oracle session `ses_fe0443831...` 验证）
   - `include/ptx_ir/ptxir_format.h:53-62` PTXIR 24B header 真实结构
   - `include/ptx_ir/ptxir_format.h:65-69` 6B TOC entry
   - `src/ptx_ir/ptxir_writer.cpp:33-82` `write_manifest_section` MANIFEST 序列化（cubin_hash + kernel_name + ptx_address_size + params + kernels[]）
   - `docs/adr/ADR-0029-ptxemu-image-executor.md`（**注意**：§D8 仍描述旧 HAL 方案，per v2 §C4 待 amendment）
   - `docs/adr/ADR-0028-multi-kernel-manifest.md` 多 kernel manifest 扩展

3. **拉取 CppTLM 仓**：`git clone https://github.com/chisuhua/CppTLM` 后核对
   - issue #19 v3.0 RFC 真实承诺
   - Mode A → Mode B 演进时间表（per ADR-090 v2 §D3）

4. **核验通过判据**：
   - tadr-308 §Decision 1.1 字段类型 (`uint64_t image_size` + `uint64_t* out_vram_addr`) 与 shipped ioctl 0x27 struct 一致
   - tadr-308 §1.2 CUmodule = GPU VA 重定义 与 ADR-090 v2 §D1 `out_vram_addr` 一致
   - tadr-308 §1.5 kernel_name 路径（Oracle A′）由应用在 `cuModuleGetFunction` 传入，**不**依赖 PTX-EMU ABI（per ADR-090 v2 §D1.2 表"❌ 不经 HAL"）
   - tadr-308 §Decision 2 "不删除" 与 tadr-301 rule 4 "FORBIDDEN" 一致（tadr-307 仅文档未 ship）

**依赖**：本任务**无前置代码**，可在 T-001 ~ T-008 实施前并行执行。

**Verify pass**：owner 在 UsrLinuxEmu PTX-EMU CppTLM 三仓 README 或 issue 评论中确认核对结果。

**Commit**: `docs(tadr-308): add T-009 父仓契约核验 trace (owner verified)`

---

## T-009b: PTX-EMU 真实 PTXIR fixture 验证（per Oracle A′ 修订 2026-08-20）

> **背景（per Oracle session `ses_fe0443831...` Part D Recommendation）**：tadr-308 §A 的 PTXIR 解析代码依赖 PTX-EMU 真实 PTXIR 格式（24B header + string_table tail + TOC + MANIFEST section）。在 shim 实施前**必须用真实 fixture 验证**，避免假设错（已教训：tadr-308 旧 §A "PTIR" magic + 16B + total_size 字段 全部错误）。

**Action**:

1. **复制 PTX-EMU fixtures 到 TaskRunner tests**:
   ```bash
   cp /workspace/project/PTX-EMU/tests/ptxir/fixtures/multi_kernel_basic.ptxir \
      tests/umd/fixtures/multi_kernel_basic.ptxir
   cp /workspace/project/PTX-EMU/tests/ptxir/fixtures/cute_rmsnorm.ptxir \
      tests/umd/fixtures/cute_rmsnorm.ptxir
   ```

2. **写一次性验证脚本** (`tools/verify_ptxir_format.sh`):
   ```bash
   #!/bin/bash
   set -e
   # 验证 magic = "PTXI" + 24B header
   for f in tests/umd/fixtures/*.ptxir; do
     magic=$(head -c 4 "$f")
     if [ "$magic" != "PTXI" ]; then
       echo "FAIL: $f magic = '$magic', expected 'PTXI'"
       exit 1
     fi
     # 验证 header_size = 24 at offset 20-23 (LE)
     hs=$(od -An -tx1 -N 4 -j 20 "$f" | tr -d ' \n')
     hs_dec=$((16#$hs))
     if [ "$hs_dec" != "24" ]; then
       echo "FAIL: $f header_size = $hs_dec, expected 24"
       exit 1
     fi
     # 验证 string_table_offset + string_table_size = file size (字符串表在尾部)
     st_off=$(od -An -tu4 -N 4 -j 12 "$f" | tr -d ' \n ')
     st_size=$(od -An -tu4 -N 4 -j 16 "$f" | tr -d ' \n ')
     file_size=$(stat -c%s "$f")
     if [ "$((st_off + st_size))" != "$file_size" ]; then
       echo "FAIL: $f string_table end ($((st_off + st_size))) != file_size ($file_size)"
       exit 1
     fi
     echo "OK: $f (magic=PTXI, header_size=24, total_size=$file_size)"
   done
   ```

3. **写 TaskRunner C++ fixture reader 单元测试** (`tests/umd/test_ptxir_format.cpp`):
   ```cpp
   TEST_CASE("PTXIR 24B header validation against real fixtures", "[tadr-308][T-009b]") {
     // Read tests/umd/fixtures/multi_kernel_basic.ptxir
     // Verify magic "PTXI", version=4, header_size=24
     // Verify total_size = string_table_offset + string_table_size
   }

   TEST_CASE("ptxir_total_size() helper works on real fixture", "[tadr-308][T-009b]") {
     // Load fixture, call shim helper, verify returned size == file size
   }
   ```

4. **MANIFEST 解析验证** (multi_kernel fixture 含多个 kernel):
   ```cpp
   TEST_CASE("ptxir_list_kernel_names() returns multi-kernel fixture names", "[tadr-308][T-009b]") {
     // Load multi_kernel_basic.ptxir
     // Parse TOC, find MANIFEST section
     // Iterate kernels[] vector (per ADR-0028)
     // Assert non-empty vector of kernel names
   }
   ```

**依赖**：本任务**无前置代码**，可与 T-009 同步并行执行。

**Verify pass**: `bash tools/verify_ptxir_format.sh` PASS + `ctest -R tadr-308` PASS。

**Commit**: `test(ptxir): validate 24B header + MANIFEST parse against real PTX-EMU fixtures (tadr-308 T-009b)`

---

## T-010: ~~ADR-090 v1 §D4 amend 同步（Gate #21）~~ — **DROPPED per Oracle 修订 2026-08-20**

> **删除原因**：ADR-090 v1 已被 ADR-090 v2 ✅ Accepted 取代（per UsrLinuxEmu `docs/00_adr/README.md` v2 = canonical），amend Superseded 文档是浪费。如需 v1 §D4 描述修正，**在 v2 或 tadr-308 自身补充**即可（v2 §D1.2 表已涵盖 kernel_name ❌ 不经 HAL/ioctl 关键事实）。
>
> **替代措施**：已合入 tadr-308 §1.5 修订块（per Commit 2）+ tadr-308 §Consequences 修订（per Commit 4）。无需独立 T-010 任务。
>
> **保留为历史 changelog**（不删除段落，记录决策变更原因）:

旧 T-010 内容保留如下作 changelog 备份：

> **Oracle 第三轮审查 M8**（2026-08-18 session `ses_feaa41dfaffeVTyUSpzNH5FDx2`）：tadr-308 §Decision 2 "append-only" 与 ADR-090 v1 §D4 "删除 3 纯虚方法" 描述表面矛盾。真相是 tadr-307 仅有文档无 ship 代码（无代码可删），需跨仓文档同步。
>
> **Action**（已 DROPPED）：
> 1. **tadr-308 端**（已完成）：`docs/shared/adr/tadr-308-igpu-driver-vram-load.md` §Decision 2 加 "M8 警告" 段；说明 tadr-307 实际仅有文档无 ship 代码
> 2. **UsrLinuxEmu 端**（DROPPED）：amend `docs/00_adr/adr-090-ptxir-via-h2d-dma.md` §D4 描述（v1 已 Superseded，改为在 v2 §D1.2 表或 tadr-308 §Consequences 修订）
>
> 关联: chisuhua/UsrLinuxEmu ADR-090 v2 commit `e03b5a1`

> **Oracle 第三轮审查 M8**：tadr-308 §Decision 2 "append-only" 与 ADR-090 v1 §D4 "删除 3 纯虚方法" 描述表面矛盾。真相是 tadr-307 仅有文档无 ship 代码（无代码可删），需跨仓文档同步。

**Action**：

1. **tadr-308 端**（已完成）：
   - `docs/shared/adr/tadr-308-igpu-driver-vram-load.md` §Decision 2 加 "M8 警告" 段
   - 说明 tadr-307 实际仅有文档无 ship 代码

2. **UsrLinuxEmu 端**（需 UsrLinuxEmu owner 协调）：
   - amend `docs/00_adr/adr-090-ptxir-via-h2d-dma.md` §D4 描述
   - 原 "删除 3 纯虚方法（#48-50）" → "tadr-307 仅有文档无 ship 代码，无需删除"
   - 引用 tadr-308 §Decision 2 "append-only" 作为佐证

**依赖**：本任务在 T-009 完成后启动（需先确认 ADR-090 v1 真实文本）。

**Verify pass**：UsrLinuxEmu commit 合并后 `docs/00_adr/adr-090-ptxir-via-h2d-dma.md` 不再含"删除 3 方法"字样。

**Commit**: `docs(tadr-308): add T-010 ADR-090 v1 §D4 amend cross-repo coordination`

---

## T-011: 决策点代码实施汇总（owner 已决策 5 项）

> **2026-08-18 Sisyphus owner 决策结果**（5 项决策点已落地）：
> 1. ✅ C2 image_size 来源 → 方案 1（PTXIR magic 头扫描）
> 2. ✅ C1 CUmodule 命名空间 → 方案 A（统一走 VRAM-load）
> 3. ✅ M11 kernel_name 解析 → 方案 A（UMD 侧解析 PTXIR header）
> 4. ✅ M8 ADR-090 v1 §D4 amend → T-010 跨仓同步
> 5. ✅ #14 父仓契约核验 → T-009 提前实施
>
> **Oracle MF-5 门禁声明（2026-08-19 `ses_fe13e43d2ffexPXVzuMj7hcyHK`）**：
> - **§A（PTXIR magic 头扫描）+ §C（kernel_name 解析）以 T-009 完成 PTXIR 真实格式核验为硬前置**
> - **§B（cuModuleLoad VRAM-load）豁免门禁**：仅依赖 ioctl 0x27 契约（u64 image_size + u64 out_vram_addr），该契约 Oracle 已在 session `ses_fe13e43d2ffexPXVzuMj7hcyHK` 中核验父仓 `plugins/gpu_driver/shared/gpu_ioctl.h:747-779` 通过

**Action**：3 个子任务（§A / §B / §C）实施上述 3 个代码决策。

### §A: PTXIR 24B header 扫描 helper（per Oracle A′ 修订 2026-08-20）

**目的**：解决 C2 image_size 来源缺失（per Oracle session `ses_fe0443831...`）。

**真实 PTXIR v4 格式**（per [PTX-EMU `include/ptx_ir/ptxir_format.h:53-62`](../../../../../PTX-EMU/include/ptx_ir/ptxir_format.h)）：
- header 24B: `magic[4] + version u16 + flags u16 + section_count u16 + reserved u16 + string_table_offset u32 + string_table_size u32 + header_size u32`
- **layout 字符串表在尾部**（per `ptxir_format.h:12` Header comment）
- **NO total_size 字段** — 推断为 `string_table_offset + string_table_size`

**Implement**（新建 `src/umd/libcuda_shim/ptxir_parser.hpp`，避免 shim 文件膨胀，与 §C 共享）：
```cpp
namespace async_task::umd::shim {

// PTX-EMU v4 header 24B (per ptxir_format.h:53-62)
constexpr char PTXIR_MAGIC[4] = {'P', 'T', 'X', 'I'};
constexpr size_t PTXIR_HEADER_SIZE = 24;

#pragma pack(push, 1)
struct ptixir_header {
  char     magic[4];              // "PTXI"
  uint16_t version;               // currently 4
  uint16_t flags;                 // must be 0
  uint16_t section_count;
  uint16_t reserved;
  uint32_t string_table_offset;   // absolute file offset
  uint32_t string_table_size;
  uint32_t header_size;           // = 24
};
#pragma pack(pop)

static_assert(sizeof(ptixir_header) == 24, "PTXIR header must be 24B");

// §A: 推断 image 总字节数 (per layout "string table at end")
// 失败返回 0 (non-PTXIR 或 header 损坏)
inline uint64_t ptxir_total_size(const void* image) {
  if (!image) return 0;
  const uint8_t* p = static_cast<const uint8_t*>(image);
  ptixir_header hdr;
  std::memcpy(&hdr, p, PTXIR_HEADER_SIZE);
  if (std::memcmp(hdr.magic, PTXIR_MAGIC, 4) != 0) return 0;  // non-PTXIR
  if (hdr.header_size != PTXIR_HEADER_SIZE) return 0;          // sanity check
  return static_cast<uint64_t>(hdr.string_table_offset) +
         static_cast<uint64_t>(hdr.string_table_size);
}

}  // namespace async_task::umd::shim
```

**集成**：T-002 `cuModuleLoadData` 调用前：
```cpp
uint64_t image_size = ptxir_total_size(image);
if (image_size == 0) return CUDA_ERROR_INVALID_VALUE;  // non-PTXIR → use LoadDataEx
```

**T-009b 验证**：用 `tests/umd/fixtures/multi_kernel_basic.ptxir` 跑 `ptxir_total_size()` 必须返回真实文件大小（156 字节）。

**Commit**: `feat(shim): add PTXIR 24B header scanner for image_size (T-011 §A per Oracle A')`

### §B: `cuModuleLoad` 改走 VRAM-load 路径（方案 A 实施）

**目的**：解决 C1 CUmodule 命名空间冲突（统一整数/GPU VA 命名空间）。

**Implement**（`src/umd/libcuda_shim/cu_module.cpp:66` 修订）：
```cpp
// === Add CUDA_ERROR_FILE_NOT_FOUND to cuda.h ===
#ifndef CUDA_ERROR_FILE_NOT_FOUND
#define CUDA_ERROR_FILE_NOT_FOUND  500
#endif

extern "C" CUresult cuModuleLoad(CUmodule* module, const char* fname) {
  if (!module || !fname) return CUDA_ERROR_INVALID_VALUE;

  // 1. 读文件到 host memory
  std::lock_guard<std::mutex> lock(g_handles.mu);
  FILE* f = fopen(fname, "rb");
  if (!f) return CUDA_ERROR_FILE_NOT_FOUND;
  fseek(f, 0, SEEK_END);
  size_t image_size = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> image(image_size);
  if (fread(image.data(), 1, image_size, f) != image_size) {
    fclose(f);
    return CUDA_ERROR_UNKNOWN;
  }
  fclose(f);

  // 2. 调 load_kernel_module 拿到 GPU VA
  uint64_t vram_addr = 0;
  int rc = runtime()->load_kernel_module(image.data(), image_size, &vram_addr);
  if (rc != 0) return cuda_error_from_errno(rc);

  // 3. CUmodule = GPU VA（统一所有模块来源）
  *module = reinterpret_cast<CUmodule>(vram_addr);
  g_handles.mod_to_name[*module] = fname;
  return CUDA_SUCCESS;
}
```

**注意**：原 `cuModuleLoad` 路径 (`next_id.fetch_add(1)`) 已废弃，所有 CUmodule 统一为 GPU VA。

> **Oracle MF-4 / D6 警告（2026-08-19 `ses_fe13e43d2ffexPXVzuMj7hcyHK`）**：
> `cu_module.cpp:82` `cuModuleGetFunction` 与原 `cuModuleLoad`（已废弃）共享 `g_handles.next_id` 计数器。
> §B 改 `cuModuleLoad` 为 VRAM-load 后，`cuModuleGetFunction` 必须同步改为独立 `next_func_id` 计数器，避免函数 ID 与模块 ID（GPU VA）撞号。
>
> **修改示例**：
> ```cpp
> // g_handles 结构体加:
> std::atomic<uint64_t> next_func_id{1};  // 独立于 next_id (CUmodule 已改为 VA)
>
> // cu_module.cpp:82 cuModuleGetFunction 改:
> //  *hfunc = reinterpret_cast<CUfunction>(g_handles.next_id.fetch_add(1));
> // 改为:
> *hfunc = reinterpret_cast<CUfunction>(g_handles.next_func_id.fetch_add(1));
> ```
>
> 这与仓内既有惯例一致（`cu_array.cpp`/`cu_event.cpp`/`cu_stream.cpp` 每张 HandleTable 都有独立 `next_id`）。

**Commit**: `feat(cu_module): unify cuModuleLoad + cuModuleLoadData to VRAM-load path + independent func counter (T-011 §B)`

### §C: UMD 侧 PTXIR MANIFEST 解析（optional validation, **DEFERRED per Oracle A′**）

> **状态变更（per Oracle session `ses_fe0443831...` A′ 2026-08-20）**：tadr-308 §1.5 方案 A 降级为 **可选 MANIFEST 验证**。
>
> **降级原因**：
> 1. launch 流程**从未**需要 UMD 从 PTXIR 提取 kernel_name（kernel_name 由应用在 `cuModuleGetFunction` 传入）
> 2. §C 旧实现（16B header + 自定义 `ptixir_kernel_entry` 在 offset 16）假设全部错误
> 3. 仅当希望 `CUDA_ERROR_NOT_FOUND` 提前错误（typo kernel_name 检测）时才需要 §C
>
> **PoC 阶段**：本任务 **DEFERRED**（不在 T-001 ~ T-008 实施范围）。未来增强：
> - 若 owner 决策需要 typo 检测，**单独发起** sub-change
> - 实施格式：24B header + TOC walk (找 type=6 MANIFEST) + iterate kernels[] 向量 (per ADR-0028)
>
> **占位实施**（per ADR-0028 多 kernel manifest）：
> ```cpp
> // 追加到 §A 的 src/umd/libcuda_shim/ptxir_parser.hpp (24B header 共享)
> namespace async_task::umd::shim {
>
> #pragma pack(push, 1)
> struct ptixir_toc_entry {
>   uint8_t  type;                  // PtxirSectionType (REGDECL=1, ..., MANIFEST=6)
>   uint8_t  reserved;
>   uint32_t offset;                // absolute file offset
> };
> #pragma pack(pop)
>
> // 列出 MANIFEST section 内 kernels[] 向量 (per ADR-0028 + ptxir_writer.cpp:33-82)
> inline std::vector<std::string>
> ptxir_list_kernel_names(const void* image, uint64_t image_size) {
>   std::vector<std::string> names;
>   if (!image || image_size < PTXIR_HEADER_SIZE) return names;
>   const uint8_t* p = static_cast<const uint8_t*>(image);
>   ptixir_header hdr;
>   std::memcpy(&hdr, p, PTXIR_HEADER_SIZE);
>   if (std::memcmp(hdr.magic, PTXIR_MAGIC, 4) != 0) return names;
>
>   // 1. walk TOC entries
>   for (uint16_t i = 0; i < hdr.section_count; ++i) {
>     size_t toc_off = PTXIR_HEADER_SIZE + i * sizeof(ptixir_toc_entry);
>     if (toc_off + sizeof(ptixir_toc_entry) > image_size) break;
>     ptixir_toc_entry toc;
>     std::memcpy(&toc, p + toc_off, sizeof(toc));
>     if (toc.type != 6 /* MANIFEST */) continue;  // only interested in MANIFEST
>
>     // 2. parse MANIFEST section (per ptxir_writer.cpp:33-82)
>     // - cubin_hash (32B) skip
>     // - kernel_name (NUL-terminated string) skip (v1 compat)
>     // - ptx_address_size (u8) skip
>     // - params (u16 count + N × entries) skip
>     // - kernels: u16 count + N × (NUL string + u32 arg_count + u32 arg_byte_size)
>     const uint8_t* mp = p + toc.offset;
>     const uint8_t* mend = mp + (image_size - toc.offset);
>     mp += 32;  // skip cubin_hash
>     while (mp < mend && *mp) ++mp;  // skip kernel_name NUL-terminated
>     ++mp;
>     if (mp >= mend) return names;
>     ++mp;  // skip ptx_address_size
>     if (mp + 2 > mend) return names;
>     uint16_t param_count;
>     std::memcpy(&param_count, mp, 2);
>     mp += 2;
>     for (uint16_t pi = 0; pi < param_count && mp < mend; ++pi) {
>       while (mp < mend && *mp) ++mp;  // param name
>       ++mp;
>       mp += 2;  // param size
>       ++mp;     // param kind
>     }
>     if (mp + 2 > mend) return names;
>     uint16_t kernel_count;
>     std::memcpy(&kernel_count, mp, 2);
>     mp += 2;
>     for (uint16_t ki = 0; ki < kernel_count && mp < mend; ++ki) {
>       const char* name_start = reinterpret_cast<const char*>(mp);
>       while (mp < mend && *mp) ++mp;  // kernel name NUL-terminated
>       names.emplace_back(name_start, mp - reinterpret_cast<const uint8_t*>(name_start));
>       ++mp;
>       mp += 8;  // skip arg_count u32 + arg_byte_size u32
>     }
>     return names;
>   }
>   return names;
> }
>
> }  // namespace async_task::umd::shim
> ```
>
> **集成** (optional, 仅在 typo 检测 enabled 时):
> ```cpp
> // cu_module.cpp cuModuleGetFunction 内:
> if (g_handles.validation_enabled.count(*module)) {
>   auto names = ptxir_list_kernel_names(image_bytes_, image_size_);
>   if (std::find(names.begin(), names.end(), name) == names.end()) {
>     return CUDA_ERROR_NOT_FOUND;
>   }
> }
> ```
>
> **Commit** (when implemented): `feat(shim): optional MANIFEST validation for CUDA_ERROR_NOT_FOUND (T-011 §C deferred)`

**T-011 实施依赖**：T-000a (CudaRuntimeApi) + T-000b (cuda_error_from_errno) + T-002 + T-009 + T-010 全部 ✅

**Verify pass**：3 个 commit 全部落地 + `ctest -R tadr-308` PASS。

---

## T-013 NEW: DISPATCH_KERNEL packet payload 规格 — G1 launch 路径修复（per Oracle 2026-08-20）

> **Oracle 重大发现（per session `ses_fe0443831...` Part B G1）**：
> 现行 `cuLaunchKernel` → `runtime()->launch_kernel(name, ...)` → `CudaScheduler::submit_launch(stream_id, kernel_index, ...)` → `IGpuDriver::submit_launch` (`igpu_driver.hpp:219`)
> **VRAM 加载的 image 在 `vram_addr` 永远不会被 launch 路径引用**。
>
> **问题**：`submit_launch` 接 `kernel_index`（不是 vram_addr）；CudaRuntimeApi 需要手动 `register_kernel()`；VRAM-load 路径返回的 vram_addr 与 kernel_index 无映射。
>
> **修复方向**（per Oracle Part D Recommendation 2）：
> 不新增 IGpuDriver 方法（per ADR-023 append-only），而是扩展 `submit_batch` (igpu_driver.hpp:191) 携带新 packet payload：
> **DISPATCH_KERNEL packet** = `{vram_addr(u64), kernel_name(str), grid/block/args/smem}`
>
> CppTLM Mode B (per ADR-090 v2 §D3.3) SQ/CQ doorbell 收到 DISPATCH_KERNEL packet 后，调 `ptxemu_image_execute_named` 执行。

**Write failing test**:
```cpp
TEST_CASE("cuLaunchKernel after cuModuleLoadData submits DISPATCH_KERNEL with vram_addr",
          "[tadr-308][T-013]") {
    // 1. load PTXIR image → vram_addr
    CUmodule mod;
    const uint8_t ptxir[] = {/* real PTXIR fixture bytes */};
    REQUIRE(cuModuleLoadData(&mod, ptxir) == CUDA_SUCCESS);
    uint64_t expected_vram = (uint64_t)mod;  // CUmodule = vram_addr

    // 2. get function
    CUfunction fn;
    REQUIRE(cuModuleGetFunction(&fn, mod, "my_kernel") == CUDA_SUCCESS);

    // 3. launch — expect submit_batch with DISPATCH_KERNEL packet
    void* args[] = {nullptr};
    REQUIRE(cuLaunchKernel(fn, 1, 1, 1, 1, 1, 1, 0, nullptr, args, nullptr) ==
            CUDA_SUCCESS);

    // 4. verify mock received DISPATCH_KERNEL with vram_addr + name
    auto& mock = *mock_drv();
    REQUIRE(mock.last_submit_batch_packet_type() == GPU_OP_DISPATCH_KERNEL);
    REQUIRE(mock.last_submit_batch_vram_addr() == expected_vram);
    REQUIRE(mock.last_submit_batch_kernel_name() == "my_kernel");
}
```

**Verify fail**: 当前 `cuLaunchKernel` (`cu_launch.cpp:62-95`) 调 `runtime()->launch_kernel(name, ...)`，仅传 kernel name，**vram_addr 丢失**。Mock 收不到 DISPATCH_KERNEL packet。

**Implement**:

1. **新增 `DISPATCH_KERNEL` packet payload 结构** (在 `include/shared/igpu_driver.hpp` 或新 `include/shared/dispatch_kernel_packet.hpp`):
   ```cpp
   namespace async_task::shared {
   constexpr uint32_t GPU_OP_DISPATCH_KERNEL = 0x04;  // 新增 opcode

   struct dispatch_kernel_packet {
     uint64_t vram_addr;      // cuModuleLoadData 返回的 vram_addr (= CUmodule)
     uint32_t grid_x, grid_y, grid_z;
     uint32_t block_x, block_y, block_z;
     uint32_t shared_mem_bytes;
     uint64_t args_ptr;       // 用户态 void** kernel_args
     uint64_t args_count;
     char     kernel_name[256]; // 应用在 cuModuleGetFunction 传入
   };
   }
   ```

2. **修改 `cu_launch.cpp`** (`cu_launch.cpp:62-95`):
   ```cpp
   extern "C" CUresult cuLaunchKernel(CUfunction f, uint32_t grid_x, ...) {
     // ... 现有 grid/block/args 校验 ...
     std::string kernel_name = resolve_func_name_impl(f);
     CUmodule mod = (CUmodule)0;
     cuFuncGetModule(&mod, f);  // 反查 func_to_module[f]

     // G1 修复: 构造 DISPATCH_KERNEL packet 携带 vram_addr (= mod) + name
     dispatch_kernel_packet pkt{};
     pkt.vram_addr = reinterpret_cast<uint64_t>(mod);  // ★ vram_addr 关键
     // ... grid/block/args/smem 填充 ...
     std::strncpy(pkt.kernel_name, kernel_name.c_str(), 255);
     pkt.kernel_name[255] = '\0';

     // 调 runtime()->submit_batch (已有 IGpuDriver 方法, 不新增)
     int rc = runtime()->submit_batch(
         default_stream_id_,
         &pkt, sizeof(pkt),
         GPU_OP_DISPATCH_KERNEL);
     return cuda_error_from_errno(rc);
   }
   ```

3. **`GpuDriverClient::submit_batch` 扩展**: 接 `GPU_OP_DISPATCH_KERNEL` opcode，序列化 packet 到 `gpu_submit_batch_args` (已有 ioctl)，**不新增 ioctl**。

4. **GpuDriverClient::submit_launch 现状**: 已存在但走 `kernel_index` 路径，与 VRAM-load 路径不兼容。本任务**保留** `submit_launch` 不变（其他路径仍在用），仅扩展 `submit_batch`。

**依赖**:
- T-001~T-008 全部 ✅
- T-009 父仓契约核验 ✅ (verify UsrLinuxEmu submit_batch dispatch table 接受新 opcode)
- T-009b PTX-EMU fixture 验证 ✅ (verify DISPATCH_KERNEL packet payload 兼容真实 PTXIR)

**Verify pass**: `ctest -R tadr-308` PASS + e2e (mock) launch 路径核验 PASS。

**Commit**: `feat(cu_launch): DISPATCH_KERNEL packet carries vram_addr + name (tadr-308 T-013 G1 fix)`

---

## T-014 NEW: `unload_kernel_module` 方法评估 — Oracle 2026-08-20 评估

> **Oracle Part D Recommendation 2 延伸**（per `ses_fe0443831...`）：
> 当前 T-003 用 `get_bo_gpu_va + free_bo` 路径卸载（绕过 ioctl 0x29）。
> 备选方案：直接调 `GPU_IOCTL_UNLOAD_KERNEL_MODULE` (ioctl 0x29)，per ADR-090 v2 §D1.2 表保留。
>
> **决策**（owner 必决）：采用 T-003 路径（VA 反查 + free_bo）OR 新增 IGpuDriver `unload_kernel_module` 调 ioctl 0x29。
>
> **推荐**（per Oracle Part E A′）：
> - PoC 阶段选 **T-003 路径**（不新增 IGpuDriver 方法，per ADR-023 append-only）
> - 长期：若 ioctl 0x29 获得更明确语义（区分 unload code BO vs free BO），可单独 T-014+ sub-change 升级
>
> **本 T-014 占位为决策记录**：实际不实施代码，仅 owner 决策记录。Commit 在 T-003 完成时一并包含。

---

| Task | 描述 | TDD Phase | Commit Hash | Status |
|---|---|---|---|---|
| **T-000a** | CudaRuntimeApi 扩 load_kernel_module（C3 硬前置）| 实施 | (待 commit) | ⏳ HARD |
| **T-000b** | cuda_error_from_errno helper 定义（C4 硬前置）| 实施 | (待 commit) | ⏳ HARD |
| **T-000c** | image_size 来源 owner 决策（C2 硬前置）| 文档 | (待 commit) | ⏳ HARD |
| T-001 | 默认实现 -ENOSYS | 5 步全过 | (待 commit) | ⏳ |
| T-002 | cuModuleLoadData 适配 | 5 步全过 | (待 commit) | ⏳ |
| T-003 | cuModuleUnload 适配 | 5 步全过 | (待 commit) | ⏳ |
| T-004 | GpuDriverClient 并发安全（M3 修订）| 5 步全过 | (待 commit) | ⏳ |
| T-005 | 错误注入测试（M3 修订）| 5 步全过 | (待 commit) | ⏳ |
| **T-005b** | cuModuleUnload 清理 func_to_* 表（M1 修复）| 5 步全过 | (待 commit) | ⏳ |
| T-006 | tadr-307 STALE 标注 | 文档任务 | ✅ 已完成 (前轮) | ✅ |
| T-007 | openspec change 归档 | Phase 2 | (待 commit) | ⏳ |
| **T-008** | 更新既有测试预期（C5 修复）| 5 步全过 | (待 commit) | ⏳ |

**预计总 commit 数**: 11-12 个 atomic commit（T-000a/b/c + T-001~T-008 + 文档 commit）

## 跨仓依赖

- **T-000a/b/c 实施顺序**：必须先完成这 3 项硬前置，否则 T-002/T-003 编译失败
- T-001 ~ T-008 实施前需 `cd external && git fetch origin main && git checkout origin/main` 确保 submodule 同步
- T-007 触发需外部条件: PTX-EMU HSK-6 + CppTLM P0-1 + TaskRunner owner ack + Oracle 8 项硬前置 (#7-#14) 全部 ✅

## References

- [proposal.md](proposal.md) (Why + What Changes + Acceptance + Cross-Repo)
- [tadr-308](../shared/adr/tadr-308-igpu-driver-vram-load.md) (canonical 设计，含 Oracle C1/C2/M1/M3/M4 警告)
- [tadr-307 STALE](../shared/adr/tadr-307-igpu-driver-kernel-module-extension.md) (历史记录)
- UsrLinuxEmu ADR-090 v1 commit `37a91b6`
- Oracle session `ses_fef78854dffeLfDJh7p8ELuMLy` (4 轮评估, v2 决策)
- Oracle session `ses_ff2106f84ffeM2oItBEa9iu4hL` (v1 启动，识别 ADR-076 v1 违规)
- **Oracle session `ses_feb85d969ffe0qPwACwwapfXen`** (2026-08-18 深度审查：5 CRITICAL + 5 MAJOR + 4 MINOR)
- **Oracle session `ses_feaa41dfaffeVTyUSpzNH5FDx2`** (2026-08-18 第二轮调研 + 审查: 2 CRITICAL + 6 MAJOR + 3 MINOR，含 C6 ADR-090 v1 引用 / C7 image_size 类型 / M6 HAL #66 引用 / M7 PTX-EMU 说明 / M8 ADR-090 v1 §D4 矛盾 / M10 MAX_KERNEL_IMAGE_SIZE / M11 kernel_name 字段缺失重大新发现)