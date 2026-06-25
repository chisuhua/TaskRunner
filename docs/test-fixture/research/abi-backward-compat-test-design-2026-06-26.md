---
SCOPE: TEST-FIXTURE
STATUS: DRAFT
---

# H-7 Issue #1 ABI 向后兼容测试设计

> **状态**: 📋 DRAFT (2026-06-26, H-3.8 协调建立)
> **作者**: TaskRunner owner
> **目标**: 为 ADR-034 §Issue #1 (stream_id u32 → u64 ABI 拓宽) 设计 ABI 向后兼容测试套件
> **关联**: openspec change `2026-06-26-h3-8-issue-1-coordination`

## 1. 测试套件总览

| 套件名 | 端 | 范围 | 优先级 | 实施时机 |
|-------|----|----|-------|---------|
| `test_h7_issue_1_old_caller_compat` | TaskRunner (MockGpuDriver) | 旧调用方兼容 | P0 | 立即（mock 端独立）|
| `test_h7_issue_1_new_caller_compat` | TaskRunner (MockGpuDriver) | 新调用方兼容 | P0 | 立即（mock 端独立）|
| `test_h7_issue_1_mixed_callers` | TaskRunner (MockGpuDriver) | 混合调用方 | P0 | 立即（mock 端独立）|
| `test_h7_issue_1_deprecation_warning` | TaskRunner (MockGpuDriver) | 废弃警告日志 | P1 | 立即（mock 端独立）|
| `test_h7_issue_1_dual_driver_compat` | UsrLinuxEmu (gpgpu_device) | 双驱动版本共存 | P0 | UsrLinuxEmu PR 1 merged 后 |
| `test_h7_issue_1_cross_process_compat` | UsrLinuxEmu (gpgpu_device) | 跨进程兼容 | P1 | UsrLinuxEmu PR 1 merged 后 |

## 2. Mock 端测试设计（TaskRunner 端立即可做）

### 2.1 `test_h7_issue_1_old_caller_compat` — 旧调用方兼容测试

**目标**: 验证旧调用方（使用 `stream_id_compat` u32 字段）在 ABI 拓宽后仍正常工作。

**测试场景**:

```cpp
// tests/test_fixture/test_h7_issue_1_old_caller_compat.cpp

#include <doctest/doctest.h>
#include "shared/igpu_driver.hpp"
#include "test_fixture/mock_gpu_driver.hpp"

TEST_CASE("H-7 Issue #1: old caller — stream_id_compat only (legacy mode)") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    // 创建 queue
    auto queue = driver.create_queue(0, 0, 0, 0);
    REQUIRE(queue != 0);
    
    // 模拟旧调用方：只设置 stream_id_compat，不设置 stream_id
    // 在 mock 中，这对应于：
    // gpu_pushbuffer_args args;
    // args.stream_id = 0;           // 旧调用方不设置（或设为 0）
    // args.stream_id_compat = static_cast<uint32_t>(queue);  // 旧调用方设置 u32
    
    // MockGpuDriver 需要支持 fallback 逻辑
    int result = driver.submit_batch_with_compat(queue, nullptr, 0, 0, 0);
    CHECK(result == 0);  // 应成功（fallback 到 stream_id_compat）
}

TEST_CASE("H-7 Issue #1: old caller — stream_id=0, stream_id_compat=valid") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    auto queue = driver.create_queue(0, 0, 0, 0);
    
    // 旧调用方：stream_id = 0, stream_id_compat = queue (u32)
    int result = driver.submit_batch_with_args({
        .stream_id = 0,                           // 新字段未设置
        .stream_id_compat = static_cast<uint32_t>(queue),  // 旧字段设置
        .count = 1,
        .va_space_handle = 0
    });
    CHECK(result == 0);  // fallback 到 stream_id_compat，应成功
}

TEST_CASE("H-7 Issue #1: old caller — stream_id=0, stream_id_compat=0 (guard)") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    // stream_id = 0, stream_id_compat = 0 — 这是旧调用方的 guard 值
    int result = driver.submit_batch_with_args({
        .stream_id = 0,
        .stream_id_compat = 0,
        .count = 1,
        .va_space_handle = 0
    });
    CHECK(result == 0);  // guard 值，应成功（与旧代码一致）
}
```

### 2.2 `test_h7_issue_1_new_caller_compat` — 新调用方兼容测试

**目标**: 验证新调用方（使用 `stream_id` u64 字段）正常工作。

**测试场景**:

```cpp
TEST_CASE("H-7 Issue #1: new caller — stream_id=u64, stream_id_compat=0") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    auto queue = driver.create_queue(0, 0, 0, 0);
    
    // 新调用方：stream_id = queue (u64), stream_id_compat = 0 (未设置)
    int result = driver.submit_batch_with_args({
        .stream_id = queue,                        // 新字段设置 u64
        .stream_id_compat = 0,                    // 旧字段未设置
        .count = 1,
        .va_space_handle = 0
    });
    CHECK(result == 0);  // 直接使用 stream_id，应成功
}

TEST_CASE("H-7 Issue #1: new caller — stream_id=UINT32_MAX+1, stream_id_compat=0") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    driver.set_next_queue_handle(static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1);
    auto queue = driver.create_queue(0, 0, 0, 0);
    
    // 新调用方：stream_id = UINT32_MAX+1 (u64), stream_id_compat = 0
    int result = driver.submit_batch_with_args({
        .stream_id = queue,                        // 新字段设置 u64（> UINT32_MAX）
        .stream_id_compat = 0,                    // 旧字段未设置
        .count = 1,
        .va_space_handle = 0
    });
    CHECK(result == 0);  // 新调用方支持 u64，应成功
    // 旧调用方无法支持（stream_id_compat 是 u32，无法传 > UINT32_MAX 的值）
}
```

### 2.3 `test_h7_issue_1_mixed_callers` — 混合调用方测试

**目标**: 验证系统中同时存在旧调用方和新调用方时，两者都正常工作。

**测试场景**:

```cpp
TEST_CASE("H-7 Issue #1: mixed callers — old + new in same system") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    // 旧调用方创建 queue
    auto old_queue = driver.create_queue(0, 0, 0, 0);
    
    // 新调用方创建 queue（> UINT32_MAX）
    driver.set_next_queue_handle(static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1);
    auto new_queue = driver.create_queue(0, 0, 0, 0);
    
    // 旧调用方提交（通过 stream_id_compat）
    int old_result = driver.submit_batch_with_args({
        .stream_id = 0,
        .stream_id_compat = static_cast<uint32_t>(old_queue),
        .count = 1,
        .va_space_handle = 0
    });
    CHECK(old_result == 0);  // fallback 成功
    
    // 新调用方提交（通过 stream_id u64）
    int new_result = driver.submit_batch_with_args({
        .stream_id = new_queue,
        .stream_id_compat = 0,
        .count = 1,
        .va_space_handle = 0
    });
    CHECK(new_result == 0);  // 直接 u64 成功
    
    // 验证：两个 queue 同时存在，互不干扰
    CHECK(driver.get_queue_count() == 2);
}

TEST_CASE("H-7 Issue #1: mixed callers — cross-va-space compatibility") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    // 创建两个 VA space
    auto va_space_1 = driver.create_va_space(0);
    auto va_space_2 = driver.create_va_space(0);
    
    // 旧调用方在 va_space_1 创建 queue
    auto queue_1 = driver.create_queue(va_space_1, 0, 0, 0);
    
    // 新调用方在 va_space_2 创建 queue (> UINT32_MAX)
    driver.set_next_queue_handle(static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1);
    auto queue_2 = driver.create_queue(va_space_2, 0, 0, 0);
    
    // 旧调用方提交到 va_space_1
    int result_1 = driver.submit_batch_with_args({
        .stream_id = 0,
        .stream_id_compat = static_cast<uint32_t>(queue_1),
        .count = 1,
        .va_space_handle = va_space_1
    });
    CHECK(result_1 == 0);
    
    // 新调用方提交到 va_space_2
    int result_2 = driver.submit_batch_with_args({
        .stream_id = queue_2,
        .stream_id_compat = 0,
        .count = 1,
        .va_space_handle = va_space_2
    });
    CHECK(result_2 == 0);
}
```

### 2.4 `test_h7_issue_1_deprecation_warning` — 废弃警告日志测试

**目标**: 验证当旧调用方使用 `stream_id_compat` 时，系统记录正确的 deprecation warning。

**测试场景**:

```cpp
TEST_CASE("H-7 Issue #1: deprecation warning — old caller triggers warning") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    auto queue = driver.create_queue(0, 0, 0, 0);
    
    // 捕获 stderr
    // 旧调用方提交
    int result = driver.submit_batch_with_args({
        .stream_id = 0,
        .stream_id_compat = static_cast<uint32_t>(queue),
        .count = 1,
        .va_space_handle = 0
    });
    CHECK(result == 0);
    
    // 验证: stderr 包含 deprecation warning
    // 格式: "[DEPRECATION] stream_id_compat is deprecated. Use stream_id (u64) instead. "
    //       "Deprecation period: 2026-06-26 ~ 2026-12-26. "
    //       "caller_pid=<pid>, legacy_handle=<handle>"
}

TEST_CASE("H-7 Issue #1: deprecation warning — new caller does NOT trigger warning") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    auto queue = driver.create_queue(0, 0, 0, 0);
    
    // 捕获 stderr
    // 新调用方提交
    int result = driver.submit_batch_with_args({
        .stream_id = queue,
        .stream_id_compat = 0,
        .count = 1,
        .va_space_handle = 0
    });
    CHECK(result == 0);
    
    // 验证: stderr 不包含 deprecation warning
}

TEST_CASE("H-7 Issue #1: deprecation warning — warn level (not error)") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    auto queue = driver.create_queue(0, 0, 0, 0);
    
    // 旧调用方提交
    driver.submit_batch_with_args({
        .stream_id = 0,
        .stream_id_compat = static_cast<uint32_t>(queue),
        .count = 1,
        .va_space_handle = 0
    });
    
    // 验证: 日志级别是 WARN，不是 ERROR
    // 避免 alerting 系统误报
}
```

## 3. 真实端测试设计（UsrLinuxEmu PR 1 merged 后可做）

### 3.1 `test_h7_issue_1_dual_driver_compat` — 双驱动版本共存测试

**位置**: `UsrLinuxEmu/tests/gpu/test_h7_issue_1_dual_driver_compat.cpp`

**测试场景**:

```cpp
// UsrLinuxEmu/tests/gpu/test_h7_issue_1_dual_driver_compat.cpp (FUTURE)

#include <catch_amalgamated.hpp>
#include "plugins/gpu_driver/drv/gpgpu_device.h"

TEST_CASE("H-7 Issue #1: dual driver — old driver + new driver") {
    // 模拟旧 driver（u32 ABI）和新 driver（u64 ABI）同时加载
    // 旧 driver: stream_id = u32, stream_id_compat = 不存在
    // 新 driver: stream_id = u64, stream_id_compat = u32 deprecated
    
    // 验证: 旧 driver 的 queue 在新 driver 中仍可访问（通过 stream_id_compat）
    // 验证: 新 driver 的 queue (> UINT32_MAX) 在旧 driver 中不可访问（预期行为）
}

TEST_CASE("H-7 Issue #1: dual driver — cross-driver submit") {
    // 旧 driver 创建的 queue，在新 driver 中提交（通过 stream_id_compat）
    // 新 driver 创建的 queue (> UINT32_MAX)，在旧 driver 中提交（预期失败）
}
```

### 3.2 `test_h7_issue_1_cross_process_compat` — 跨进程兼容测试

**测试场景**:

```cpp
TEST_CASE("H-7 Issue #1: cross-process — old process + new process") {
    // Process A (old caller): 使用 u32 stream_id_compat
    // Process B (new caller): 使用 u64 stream_id
    
    // 验证: 两个进程可以同时使用同一个 GPU 设备
    // 验证: 各自的 queue 互不干扰
}
```

## 4. 跨仓对称性验证

| 行为 | TaskRunner Mock 端 | UsrLinuxEmu Real 端 | 对称性 |
|------|-------------------|--------------------|-----|
| 旧调用方 stream_id=0, compat=valid | fallback 成功 | fallback 成功 | ✅ |
| 旧调用方 stream_id=0, compat=0 | guard 成功 | guard 成功 | ✅ |
| 新调用方 stream_id=u64, compat=0 | 直接成功 | 直接成功 | ✅ |
| 混合调用方同时存在 | 互不干扰 | 互不干扰 | ✅ |
| deprecation warning | WARN 级别 | WARN 级别 | ✅ |
| 跨进程兼容 | 支持 | 支持 | ✅ |

## 5. 实施优先级

### 立即（PR 1 之前可做）

1. **测试设计文档**（本文档）✅
2. **Mock 端测试骨架**（不依赖 UsrLinuxEmu 修复）：
   - `test_h7_issue_1_old_caller_compat` 骨架（3 cases）
   - `test_h7_issue_1_new_caller_compat` 骨架（2 cases）
   - `test_h7_issue_1_mixed_callers` 骨架（2 cases）
   - `test_h7_issue_1_deprecation_warning` 骨架（3 cases）

### PR 1 之后（mock 端 PR 1 改动同步）

3. **Mock 端测试 100% 实施**
4. **真实端测试**（UsrLinuxEmu owner 端）

## 6. 关联文档

- **openspec change**: `2026-06-26-h3-8-issue-1-coordination/`
- **ADR-034** (UsrLinuxEmu): §Issue #1 canonical
- **tadr-105** (TaskRunner): §Issue #1 mirror
- **u64 边界值测试设计**: `docs/test-fixture/research/u64-boundary-test-design-2026-06-26.md`
- **AMD ROCm / NVIDIA CUDA 调研**: `docs/shared/research/gpu-queue-id-patterns-2026-06-26.md`

## 7. 维护

- **Owner**: TaskRunner owner
- **Reviewers**: UsrLinuxEmu owner（跨仓 review）
- **更新时机**: 
  - PR 1 merged 后：更新 §2 实施状态
  - PR 2 merged 后：更新 §5 优先级
- **关联实施**: TaskRunner 测试套件 + UsrLinuxEmu 测试套件