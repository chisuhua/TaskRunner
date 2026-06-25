---
SCOPE: TEST-FIXTURE
STATUS: DRAFT
---

# H-7 Issue #3 测试设计文档

> **状态**: 📋 DRAFT (2026-06-26, H-3.6 协调建立)
> **作者**: TaskRunner owner
> **目标**: 为 ADR-034 §Issue #3 (`attached_queues` 弱校验) 设计完整测试套件
> **关联**: openspec change `2026-06-26-h3-6-issue-3-coordination`

本测试设计文档为 TaskRunner owner 端**预备**的测试套件设计，**不**绑定 UsrLinuxEmu 端实际修复时序。设计目标：
1. **mock 端**测试可独立运行（不依赖 UsrLinuxEmu 实际修复）
2. **真实端**测试设计待 UsrLinuxEmu PR 1 merged 后实施
3. **跨仓对称性**：TaskRunner 端 mock 行为与 UsrLinuxEmu 端 real 行为应一致

## 1. 测试套件总览

| 套件名 | 端 | 范围 | 优先级 | 实施时机 |
|-------|----|----|-------|---------|
| `test_h7_issue_3_attached_queues_race` | TaskRunner (MockGpuDriver) | race condition | P0 | 立即（mock 端独立）|
| `test_h7_issue_3_error_code_semantics` | TaskRunner (MockGpuDriver) | 错误码语义 | P0 | 立即（mock 端独立）|
| `test_h7_issue_3_performance_baseline` | TaskRunner (MockGpuDriver) | O(1) lookup 性能 | P1 | 立即（mock 端独立）|
| `test_h7_issue_3_log_enhancement` | TaskRunner (MockGpuDriver) | 错误日志增强 | P1 | 立即（mock 端独立）|
| `test_h7_issue_3_real_integration` | UsrLinuxEmu (gpgpu_device) | 端到端真实集成 | P0 | UsrLinuxEmu PR 1 merged 后 |
| `test_h7_issue_3_usrlx_stress` | UsrLinuxEmu (gpgpu_device) | 压力测试 | P2 | UsrLinuxEmu PR 1 merged 后 |

## 2. Mock 端测试设计（TaskRunner 端立即可做）

### 2.1 `test_h7_issue_3_attached_queues_race` — race condition 测试

**目标**: 验证 `destroy_va_space` 与 `submit_batch` 并发时，**已 destroy 的 queue 拒绝接受 submit**。

**测试场景**:

```cpp
// tests/test_fixture/test_h7_issue_3_attached_queues_race.cpp

#include <doctest/doctest.h>
#include <thread>
#include <atomic>
#include "shared/igpu_driver.hpp"
#include "test_fixture/mock_gpu_driver.hpp"

TEST_CASE("H-7 Issue #3: destroy_va_space vs submit_batch race") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    SUBCASE("Scenario 1: destroy before submit (sequential)") {
        // Create VA Space
        auto va_space = driver.create_va_space(0);
        REQUIRE(va_space != 0);
        
        // Register GPU
        REQUIRE(driver.register_gpu(va_space, 0, 0) == 0);
        
        // Create queue
        auto queue = driver.create_queue(va_space, 0, 0, 0);
        REQUIRE(queue != 0);
        
        // Destroy VA Space
        REQUIRE(driver.destroy_va_space(va_space) == 0);
        
        // Attempt submit_batch with destroyed queue
        // Expected: -ENOENT (NOT -EINVAL) — issue #3 PR 2 requirement
        int result = driver.submit_batch(queue, nullptr, 0, 0, 0);
        CHECK(result == -ENOENT);  // PR 2 success
        // CHECK(result == -EINVAL);  // Current behavior (PR 1 acceptable)
    }
    
    SUBCASE("Scenario 2: concurrent destroy and submit") {
        // ... 100 iterations of concurrent destroy + submit
        // Track success/failure counts
        // Expected: NO submit_batch accepted after destroy
    }
    
    SUBCASE("Scenario 3: destroy in progress, submit arrives") {
        // ... use std::atomic<bool> to signal "destroy started"
        // ... verify submit after signal returns -ENOENT
    }
}
```

**MockGpuDriver 端需要的改动**（TaskRunner owner 端**预备**，不实施）:

```cpp
// tests/test_fixture/mock_gpu_driver.hpp (FUTURE)

class MockGpuDriver {
    // ... existing members ...
    
    // H-7 Issue #3 PR 2: error code semantics
    int submit_batch(uint64_t stream_id, gpu_gpfifo_entry* entries, 
                     uint32_t count, uint64_t va_space_handle, uint64_t* out_fence_id) override {
        // ... existing checks ...
        
        // NEW: distinguish queue-destroyed from queue-not-attached
        if (!queue_alive(stream_id)) {
            return -ENOENT;  // queue has been destroyed
        }
        if (!queue_attached(stream_id, va_space_handle)) {
            return -EINVAL;  // queue exists but not attached
        }
        
        // ... existing submit logic ...
    }
};
```

### 2.2 `test_h7_issue_3_error_code_semantics` — 错误码语义测试

**目标**: 验证 4 种错误码（`-EINVAL` / `-ENOENT` / `-EBUSY` / `0` success）正确区分。

**测试场景**:

```cpp
TEST_CASE("H-7 Issue #3: error code semantic differentiation") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    SUBCASE("Success: queue attached, va_space valid") {
        // ... setup ...
        int result = driver.submit_batch(queue, entries, 1, va_space, &fence_id);
        CHECK(result == 0);
    }
    
    SUBCASE("-EINVAL: type mismatch (queue is COMPUTE, submit is COPY)") {
        // ... setup with type mismatch ...
        int result = driver.submit_batch(queue, entries, 1, va_space, &fence_id);
        CHECK(result == -EINVAL);
    }
    
    SUBCASE("-ENOENT: queue not in attached_queues") {
        // ... setup with queue not attached ...
        int result = driver.submit_batch(unattached_queue, entries, 1, va_space, &fence_id);
        CHECK(result == -ENOENT);
    }
    
    SUBCASE("-EBUSY: queue still attached but va_space in transitioning state") {
        // ... setup with va_space destroy in progress ...
        int result = driver.submit_batch(queue, entries, 1, transitioning_va_space, &fence_id);
        CHECK(result == -EBUSY);
    }
    
    SUBCASE("Error message includes sufficient context") {
        // ... setup with error condition ...
        // Capture stderr/stdout
        // Verify: includes stream_id, va_space_handle, va_space state, queue state
    }
}
```

### 2.3 `test_h7_issue_3_performance_baseline` — O(1) lookup 性能测试

**目标**: 验证 `attached_queues.find()` 在大 N 下保持 O(1) 性能（vs `std::find` O(n)）。

**测试场景**:

```cpp
TEST_CASE("H-7 Issue #3: O(1) lookup performance baseline") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    SUBCASE("N=1000: lookup time") {
        // Create va_space with 1000 attached queues
        // Measure lookup time for 10000 iterations
        // Expected: < 1ms total (O(1) avg)
    }
    
    SUBCASE("N=10000: lookup time") {
        // Same as above with 10000 queues
        // Expected: < 10ms total (O(1) avg)
    }
    
    SUBCASE("N=100000: lookup time") {
        // Same as above with 100000 queues
        // Expected: < 100ms total (O(1) avg)
    }
    
    SUBCASE("Comparison: unordered_set vs vector (regression test)") {
        // Construct vector<uint64_t> with same data
        // Measure std::find time for same iterations
        // Expected: O(n) growth, much slower for large N
    }
}
```

### 2.4 `test_h7_issue_3_log_enhancement` — 错误日志增强测试

**目标**: 验证错误日志包含完整上下文（stream_id + va_space_handle + attached_queues state dump）。

**测试场景**:

```cpp
TEST_CASE("H-7 Issue #3: error log includes attached_queues state") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    SUBCASE("Error log: -EINVAL case") {
        // ... setup ...
        // Capture stderr
        // Verify: log message includes
        //   - stream_id (e.g., "stream_id=42")
        //   - va_space_handle (e.g., "va_space_handle=1")
        //   - attached_queues list (e.g., "attached=[1, 2, 3, ...]")
        //   - va_space state (e.g., "va_space.state=ACTIVE")
    }
    
    SUBCASE("Error log: -ENOENT case") {
        // ... similar verification ...
    }
    
    SUBCASE("Error log: -EBUSY case") {
        // ... similar verification ...
    }
}
```

## 3. 真实端测试设计（UsrLinuxEmu PR 1 merged 后可做）

### 3.1 `test_h7_issue_3_real_integration` — 端到端真实集成

**位置**: `UsrLinuxEmu/tests/gpu/test_h7_issue_3_integration.cpp`

**测试场景**:

```cpp
// UsrLinuxEmu/tests/gpu/test_h7_issue_3_integration.cpp (FUTURE)

#include <catch_amalgamated.hpp>
#include "plugins/gpu_driver/drv/gpgpu_device.h"

TEST_CASE("H-7 Issue #3 PR 1: unordered_set attached_queues integration") {
    // Setup: real GpgpuDevice
    GpgpuDevice device;
    device.initialize();
    
    SECTION("PR 1: attached_queues is unordered_set (compile-time check)") {
        // ... verify type via static_assert ...
        // static_assert(std::is_same_v<decltype(GpgpuDevice::VASpace::attached_queues),
        //                               std::unordered_set<uint64_t>>);
    }
    
    SECTION("PR 1: find() returns correct iterator") {
        // ... create va_space + queue ...
        // ... verify find() returns valid iterator ...
    }
    
    SECTION("PR 1: behavior compatible with std::find") {
        // ... compare against expected std::find behavior ...
    }
}
```

### 3.2 `test_h7_issue_3_usrlx_stress` — 压力测试

**测试场景**:

```cpp
TEST_CASE("H-7 Issue #3 stress: 100K submit/destroy operations") {
    GpgpuDevice device;
    
    // 100K iterations: create va_space, create queue, submit, destroy
    // Measure: total time, peak memory, success rate
    // Expected: 0% silent -EINVAL (all errors should be intentional)
}
```

## 4. 跨仓对称性验证

**核心原则**: TaskRunner 端 mock 行为应与 UsrLinuxEmu 端 real 行为**对称**。

**对称性检查表**:

| 行为 | TaskRunner Mock 端 | UsrLinuxEmu Real 端 | 对称性 |
|------|-------------------|--------------------|-----|
| stream_id 0 提交 | 0 (guard) | 0 (guard) | ✅ |
| stream_id 不在 attached_queues | -ENOENT (PR 2) | -ENOENT (PR 2) | ✅ |
| stream_id 在 attached_queues, valid | 0 | 0 | ✅ |
| va_space_handle 0 提交 | 0 (guard) | 0 (guard) | ✅ |
| va_space destroyed 后 submit | -ENOENT (PR 2) | -ENOENT (PR 2) | ✅ |
| type 不匹配 | -EINVAL | -EINVAL | ✅ |
| 错误日志格式 | mock format | real format | ⚠️ 可能不一致 |

**⚠️ 不一致风险**: 错误日志格式可能因 mock vs real 实现差异而不一致。这是**预期**的，不要求严格对称。

## 5. 实施优先级

### 立即（PR 1 之前可做）

1. **测试设计文档**（本文档）✅
2. **Mock 端测试骨架**（不依赖 UsrLinuxEmu 修复）：
   - `test_h7_issue_3_attached_queues_race` 骨架（不验证 -ENOENT，验证 -EINVAL）
   - `test_h7_issue_3_error_code_semantics` 骨架（仅验证 -EINVAL / 0）
   - `test_h7_issue_3_log_enhancement` 骨架（不验证新错误码）

### PR 1 之后（mock 端 PR 1 改动同步）

3. **PR 1 改动同步到 MockGpuDriver**：
   - MockGpuDriver 的 `attached_queues` 改为 `unordered_set`
   - MockGpuDriver 的 `submit_batch` 使用 `.find()`
4. **Mock 端测试 100% 实施**（包括 -ENOENT / -EBUSY 验证）
5. **真实端测试**（UsrLinuxEmu owner 端）

### PR 2 之后（错误码语义化）

6. **错误码语义完整验证**（mock 端 + real 端）
7. **race condition 完整测试**（含并发场景）
8. **压力测试**

## 6. 关联文档

- **openspec change**: `2026-06-26-h3-6-issue-3-coordination/`
- **ADR-034** (UsrLinuxEmu): §Issue #3 canonical
- **tadr-105** (TaskRunner): §Issue #3 mirror
- **AMD ROCm / NVIDIA CUDA 调研**: `docs/shared/research/queue-id-patterns-2026-06-26.md`（待创建）

## 7. 维护

- **Owner**: TaskRunner owner
- **Reviewers**: UsrLinuxEmu owner（跨仓 review）
- **更新时机**: 
  - PR 1 merged 后：更新 §2 实施状态
  - PR 2 merged 后：更新 §5 优先级
- **关联实施**: TaskRunner 测试套件 + UsrLinuxEmu 测试套件
