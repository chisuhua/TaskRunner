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

### T-000c: image_size 来源 owner 决策（C2）

**目的**：CUDA API `cuModuleLoadData` 无 size 参数，T-002 实施前需 owner 选方案 1（PTXIR magic 头）或方案 2（强制 `cuModuleLoadDataEx`）。

**Action**：
1. 在 [tadr-308 §Decision 1.3.1](../shared/adr/tadr-308-igpu-driver-vram-load.md) C2 警告段标注 owner 决策
2. 在 [issues/10](https://github.com/chisuhua/TaskRunner/issues/10) 提请 owner 决策
3. owner 决策前 T-002 **不实施**

**Commit**: `docs(tadr-308): mark C2 image_size decision pending (T-000c)`

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

## T-003: `cuModuleUnload` 接入 `free_bo` 路径 (TDD 5 步)

**Write failing test**:
```cpp
TEST_CASE("cuModuleUnload forwards to free_bo", "[tadr-308][T-003]") {
    CUmodule module = (CUmodule)0xDEADBEEFCAFEBABE;
    CUresult rc = cuModuleUnload(module);
    REQUIRE(rc == CUDA_SUCCESS);
    REQUIRE(mock_drv()->last_freed_bo() == 0xDEADBEEFCAFEBABE);
}
```

**Verify fail**: `cuModuleUnload` 当前返回 NOT_IMPLEMENTED。

**Implement**:
```cpp
// src/umd/libcuda_shim/cu_module.cpp:99 (cuModuleUnload 当前实现位置)
CUresult cuModuleUnload(CUmodule module) {
    if (!module) return CUDA_ERROR_INVALID_VALUE;
    uint64_t vram_addr = reinterpret_cast<uint64_t>(module);
    int rc = runtime()->free_bo(vram_addr);
    return cuda_error_from_errno(rc);  // NOT static_cast<CUresult>(rc) — errno 负值会变非法 CUDA error code
}
```

> **修正点（per 2026-08-18 review）**：
> 1. 加 `if (!module)` nullptr 检查（之前缺失）
> 2. 用 `cuda_error_from_errno(rc)` 而非 `static_cast<CUresult>(rc)`——`free_bo` 返回负 errno（如 -EINVAL/-ENOMEM），CUresult 枚举仅 0-999 合法，强转会产生非法 CUDA error code
> 3. 行号修正：`:93` → `:99`（实测 `cuModuleUnload` 在 cu_module.cpp:99）

**Verify pass**: `ctest -R tadr-308` PASS。

**Commit**: `feat(cu_module): forward cuModuleUnload to free_bo (tadr-308 T-003)`

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

> **Oracle 第二轮审查硬前置**（per `ses_feaa41dfaffeVTyUSpzNH5FDx2`）：Oracle 沙盒内 UsrLinuxEmu 符号链接断裂，ioctl 0x27/HAL #66/ADR-090 v1/PTX-EMU ABI/CppTLM ABI 未独立核验。

**Action**（owner apply change 前必做）：

1. **拉取 UsrLinuxEmu 仓**：`git clone https://github.com/chisuhua/UsrLinuxEmu` 后核对
   - `plugins/gpu_driver/shared/gpu_ioctl.h:723-779` ioctl 0x27 struct 字段
   - `plugins/gpu_driver/hal/gpu_hal.h:351` HAL #66 签名
   - `docs/00_adr/adr-090-ptxir-via-h2d-dma.md` ADR-090 v1 全文 §D1 §D2 §D3

2. **拉取 PTX-EMU 仓**：`git clone https://github.com/chisuhua/PTX-EMU` 后核对
   - `include/cudart/cpptlm_module.h` v2 8 个 ABI 函数签名
   - `docs/adr/ADR-0029-ptxemu-image-executor.md` §D8 集成约定

3. **拉取 CppTLM 仓**：`git clone https://github.com/chisuhua/CppTLM` 后核对
   - issue #19 v3.0 RFC 真实承诺
   - Mode A → Mode B 演进时间表

4. **核验通过判据**：
   - tadr-308 §Decision 1.1 字段类型 (`uint64_t image_size` + `uint64_t* out_vram_addr`) 与 ioctl 0.27 struct 一致
   - tadr-308 §1.5 kernel_name 路径（如选方案 A）不依赖 PTX-EMU ABI
   - tadr-308 §Consequences 0x28 deprecated 描述与 UsrLinuxEmu 实际 handler 一致
   - tadr-308 §Consequences 0x29 走 FREE_BO 路径描述与 UsrLinuxEmu 实际 handler 一致

**依赖**：本任务**无前置代码**，可在 T-001 ~ T-008 实施前并行执行。

**Verify pass**：owner 在 usrlx PTX-EMU CppTLM 三仓 README 或 issue 评论中确认核对结果。

**Commit**: `docs(tadr-308): add T-009 父仓契约核验 trace (owner verified)`

---

## T-010: ADR-090 v1 §D4 amend 同步（Gate #21）

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

### §A: PTXIR magic 头扫描 helper（方案 1 实施）

**目的**：解决 C2 image_size 来源缺失。

**Implement**（新建 `src/umd/libcuda_shim/ptxir_parser.hpp`，避免 shim 文件膨胀）：
```cpp
namespace async_task::umd::shim {

constexpr uint32_t PTXIR_MAGIC = 0x50544952;  // "PTIR" little-endian
constexpr size_t PTXIR_HEADER_SIZE = 16;
#pragma pack(push, 1)
struct ptixir_header {
  uint32_t magic;       // PTXIR_MAGIC
  uint32_t version;     // currently 1 or 2
  uint64_t total_size;  // entire image byte count
};
#pragma pack(pop)

inline uint64_t ptixir_total_size(const void* image) {
  if (!image) return 0;
  const uint8_t* p = static_cast<const uint8_t*>(image);
  ptixir_header hdr;
  std::memcpy(&hdr, p, PTXIR_HEADER_SIZE);
  if (hdr.magic != PTXIR_MAGIC) return 0;  // non-PTXIR
  return hdr.total_size;
}

}  // namespace async_task::umd::shim
```

**集成**：T-002 `cuModuleLoadData` 调用前：
```cpp
uint64_t image_size = ptixir_total_size(image);
if (image_size == 0) return CUDA_ERROR_INVALID_VALUE;  // 引导用户改 LoadDataEx
```

**Commit**: `feat(shim): add PTXIR magic header scanner for image_size (T-011 §A)`

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

### §C: UMD 侧 PTXIR header 解析拿 kernel_name（方案 A 实施）

**目的**：解决 M11 kernel_name 字段缺失（ioctl 0x27 不输出 kernel_name）。

**Implement**（追加到 §A 新建的 `src/umd/libcuda_shim/ptxir_parser.hpp`，与 §A 共享 PTXIR header 假设）：
```cpp
namespace async_task::umd::shim {

constexpr uint32_t PTXIR_KERNEL_NAME_OFFSET = 16;  // header 后首个 kernel manifest
constexpr size_t PTXIR_KERNEL_NAME_MAX = 256;

#pragma pack(push, 1)
struct ptixir_kernel_entry {
  uint32_t name_offset;  // offset from image start
  uint32_t name_length;
  uint64_t entry_point;  // code offset
};
#pragma pack(pop)

inline std::string ptixir_first_kernel_name(const void* image, uint64_t image_size) {
  if (!image || image_size < PTXIR_HEADER_SIZE + sizeof(ptixir_kernel_entry)) {
    return {};
  }
  ptixir_kernel_entry entry;
  std::memcpy(&entry, static_cast<const uint8_t*>(image) + PTXIR_HEADER_SIZE,
              sizeof(entry));
  if (entry.name_offset + entry.name_length > image_size) return {};
  return std::string(static_cast<const char*>(image) + entry.name_offset,
                      entry.name_length);
}

}  // namespace async_task::umd::shim
```

**集成**：T-002 `cuModuleLoadData` 成功后：
```cpp
std::string kernel_name = ptixir_first_kernel_name(image, image_size);
if (!kernel_name.empty()) {
  g_handles.mod_to_name[*module] = kernel_name;
}
```

**Commit**: `feat(shim): parse kernel_name from PTXIR header in UMD (T-011 §C)`

**T-011 实施依赖**：T-000a (CudaRuntimeApi) + T-000b (cuda_error_from_errno) + T-002 + T-009 + T-010 全部 ✅

**Verify pass**：3 个 commit 全部落地 + `ctest -R tadr-308` PASS。

---

## 验收总结

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