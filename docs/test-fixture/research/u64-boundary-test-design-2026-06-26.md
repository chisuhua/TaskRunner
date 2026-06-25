---
SCOPE: TEST-FIXTURE
STATUS: DRAFT
---

# H-7 Issue #1 u64 边界值测试设计

> **状态**: 📋 DRAFT (2026-06-26, H-3.8 协调建立)
> **作者**: TaskRunner owner
> **目标**: 为 ADR-034 §Issue #1 (stream_id u32 → u64 ABI 拓宽) 设计 u64 边界值测试套件
> **关联**: openspec change `2026-06-26-h3-8-issue-1-coordination`

## 1. 测试套件总览

| 套件名 | 端 | 范围 | 优先级 | 实施时机 |
|-------|----|----|-------|---------|
| `test_h7_issue_1_u64_boundary` | TaskRunner (MockGpuDriver) | u64 边界值 | P0 | 立即（mock 端独立）|
| `test_h7_issue_1_u64_overflow` | TaskRunner (MockGpuDriver) | UINT32_MAX 溢出 | P0 | 立即（mock 端独立）|
| `test_h7_issue_1_r2_mapping_removed` | TaskRunner (MockGpuDriver) | R2 mapping 移除后行为一致性 | P0 | 立即（mock 端独立）|
| `test_h7_issue_1_u64_real_integration` | UsrLinuxEmu (gpgpu_device) | 端到端真实集成 | P0 | UsrLinuxEmu PR 1 merged 后 |
| `test_h7_issue_1_u64_stress` | UsrLinuxEmu (gpgpu_device) | 压力测试 (100K queue) | P2 | UsrLinuxEmu PR 1 merged 后 |

## 2. Mock 端测试设计（TaskRunner 端立即可做）

### 2.1 `test_h7_issue_1_u64_boundary` — u64 边界值测试

**目标**: 验证 `next_queue_handle_` 从 `UINT32_MAX - 1` 到 `UINT32_MAX + 1` 的 queue 创建和提交行为。

**测试场景**:

```cpp
// tests/test_fixture/test_h7_issue_1_u64_boundary.cpp

#include <doctest/doctest.h>
#include <limits>
#include "shared/igpu_driver.hpp"
#include "test_fixture/mock_gpu_driver.hpp"

TEST_CASE("H-7 Issue #1: u64 boundary — UINT32_MAX - 1") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    // 设置 next_queue_handle_ 为 UINT32_MAX - 1 (模拟长跑服务状态)
    driver.set_next_queue_handle(std::numeric_limits<uint32_t>::max() - 1);
    
    // 创建 queue — 应成功，返回 0xFFFFFFFE
    auto queue = driver.create_queue(0, 0, 0, 0);  // va_space_handle, flags, type, priority
    REQUIRE(queue == std::numeric_limits<uint32_t>::max() - 1);
    
    // 提交 — 应成功
    int result = driver.submit_batch(queue, nullptr, 0, 0, 0);
    CHECK(result == 0);
    
    // 验证 stream_id 在 u64 中完整保存（无截断）
    CHECK(queue == static_cast<uint64_t>(std::numeric_limits<uint32_t>::max() - 1));
}

TEST_CASE("H-7 Issue #1: u64 boundary — UINT32_MAX") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    driver.set_next_queue_handle(std::numeric_limits<uint32_t>::max());
    
    // 创建 queue — 应成功，返回 0xFFFFFFFF
    auto queue = driver.create_queue(0, 0, 0, 0);
    REQUIRE(queue == std::numeric_limits<uint32_t>::max());
    
    // 提交 — 应成功
    int result = driver.submit_batch(queue, nullptr, 0, 0, 0);
    CHECK(result == 0);
}

TEST_CASE("H-7 Issue #1: u64 boundary — UINT32_MAX + 1") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    driver.set_next_queue_handle(static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1);
    
    // 创建 queue — 应成功，返回 0x100000000
    auto queue = driver.create_queue(0, 0, 0, 0);
    REQUIRE(queue == static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1);
    
    // 提交 — 应成功（这是 PR 1 的关键验证点）
    int result = driver.submit_batch(queue, nullptr, 0, 0, 0);
    CHECK(result == 0);
    
    // 验证 u64 高位未被截断
    CHECK((queue & 0xFFFFFFFF00000000ULL) != 0);  // 高位非零
}

TEST_CASE("H-7 Issue #1: u64 boundary — UINT32_MAX + 1000") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    driver.set_next_queue_handle(static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1000);
    
    // 创建 queue — 应成功
    auto queue = driver.create_queue(0, 0, 0, 0);
    REQUIRE(queue == static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1000);
    
    // 提交 — 应成功
    int result = driver.submit_batch(queue, nullptr, 0, 0, 0);
    CHECK(result == 0);
}
```

**MockGpuDriver 端需要的改动**（TaskRunner owner 端**预备**，不实施）:

```cpp
// tests/test_fixture/mock_gpu_driver.hpp (FUTURE)

class MockGpuDriver {
    // ... existing members ...
    
    // H-7 Issue #1 PR 1: u64 ABI 支持
    void set_next_queue_handle(uint64_t handle) {
        next_queue_handle_ = handle;  // 允许测试设置任意 u64 值
    }
    
    uint64_t get_next_queue_handle() const {
        return next_queue_handle_;
    }
    
    // 修改 submit_batch 签名（内部使用 u64）
    int submit_batch(uint64_t stream_id, gpu_gpfifo_entry* entries, 
                     uint32_t count, uint64_t va_space_handle, uint64_t* out_fence_id) override {
        // ... 移除 static_cast<uint64_t>(args->stream_id) ...
        // 直接使用 stream_id 作为 u64
    }
};
```

### 2.2 `test_h7_issue_1_u64_overflow` — UINT32_MAX 溢出测试

**目标**: 验证 `UINT32_MAX` 溢出后，新 queue 创建和提交行为正确。

**测试场景**:

```cpp
TEST_CASE("H-7 Issue #1: overflow — create 1000 queues after UINT32_MAX") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    driver.set_next_queue_handle(std::numeric_limits<uint32_t>::max());
    
    // 创建 1000 个 queue，每个都 > UINT32_MAX
    std::vector<uint64_t> queues;
    for (int i = 0; i < 1000; ++i) {
        auto queue = driver.create_queue(0, 0, 0, 0);
        REQUIRE(queue > std::numeric_limits<uint32_t>::max());
        queues.push_back(queue);
    }
    
    // 验证所有 queue 唯一
    std::unordered_set<uint64_t> unique_queues(queues.begin(), queues.end());
    CHECK(unique_queues.size() == queues.size());
    
    // 验证每个 queue 可提交
    for (auto queue : queues) {
        int result = driver.submit_batch(queue, nullptr, 0, 0, 0);
        CHECK(result == 0);
    }
}

TEST_CASE("H-7 Issue #1: overflow — error log displays full u64") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    driver.set_next_queue_handle(static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1);
    auto queue = driver.create_queue(0, 0, 0, 0);
    
    // 模拟错误提交（queue 不在 attached_queues）
    // 捕获 stderr
    // 验证: 错误日志显示完整 u64 stream_id（如 "stream_id=4294967296"）
    // 而不是截断后的 u32（如 "stream_id=0"）
}
```

### 2.3 `test_h7_issue_1_r2_mapping_removed` — R2 mapping 移除后行为一致性

**目标**: 验证 R2 mapping (`LOW32` 截断) 移除后，行为与旧代码一致。

**测试场景**:

```cpp
TEST_CASE("H-7 Issue #1: R2 mapping removed — behavior consistency") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    // 设置 next_queue_handle_ = 1（正常值）
    driver.set_next_queue_handle(1);
    
    auto queue = driver.create_queue(0, 0, 0, 0);
    REQUIRE(queue == 1);
    
    // 提交 — 应成功
    int result = driver.submit_batch(queue, nullptr, 0, 0, 0);
    CHECK(result == 0);
    
    // 验证: 与旧代码（R2 mapping + static_cast）行为一致
    // 旧代码: static_cast<uint64_t>(args->stream_id) → static_cast<uint64_t>(1) = 1
    // 新代码: args->stream_id = 1（直接 u64）
    // 两者行为一致，但新代码更自然
}

TEST_CASE("H-7 Issue #1: R2 mapping removed — no truncation") {
    async_task::gpu::MockGpuDriver driver;
    driver.initialize();
    
    // 设置 next_queue_handle_ = 0x123456789ABCDEF0
    driver.set_next_queue_handle(0x123456789ABCDEF0ULL);
    
    auto queue = driver.create_queue(0, 0, 0, 0);
    REQUIRE(queue == 0x123456789ABCDEF0ULL);
    
    // 旧代码: static_cast<uint64_t>(args->stream_id) → 0x9ABCDEF0 (截断)
    // 新代码: args->stream_id = 0x123456789ABCDEF0 (完整)
    
    // 验证: 新代码不截断
    int result = driver.submit_batch(queue, nullptr, 0, 0, 0);
    CHECK(result == 0);  // 新代码应成功（完整 handle）
    // 旧代码: -EINVAL（截断后的 handle 不匹配）
}
```

## 3. 真实端测试设计（UsrLinuxEmu PR 1 merged 后可做）

### 3.1 `test_h7_issue_1_u64_real_integration` — 端到端真实集成

**位置**: `UsrLinuxEmu/tests/gpu/test_h7_issue_1_u64_integration.cpp`

**测试场景**:

```cpp
// UsrLinuxEmu/tests/gpu/test_h7_issue_1_u64_integration.cpp (FUTURE)

#include <catch_amalgamated.hpp>
#include <limits>
#include "plugins/gpu_driver/drv/gpgpu_device.h"

TEST_CASE("H-7 Issue #1 PR 1: stream_id is u64 (compile-time check)") {
    // 验证 ABI 结构体字段类型
    // static_assert(sizeof(gpu_pushbuffer_args::stream_id) == sizeof(uint64_t));
    // static_assert(sizeof(gpu_pushbuffer_args::stream_id_compat) == sizeof(uint32_t));
}

TEST_CASE("H-7 Issue #1 PR 1: UINT32_MAX - 1 submit") {
    GpgpuDevice device;
    device.initialize();
    
    // 手动设置 next_queue_handle_ 为 UINT32_MAX - 1（通过 ioctl 或内部 API）
    // 创建 queue
    // 提交 — 应成功
}

TEST_CASE("H-7 Issue #1 PR 1: UINT32_MAX + 1 submit") {
    GpgpuDevice device;
    device.initialize();
    
    // 手动设置 next_queue_handle_ 为 UINT32_MAX + 1
    // 创建 queue
    // 提交 — 应成功（PR 1 关键验证点）
}

TEST_CASE("H-7 Issue #1 PR 1: error log displays full u64") {
    GpgpuDevice device;
    device.initialize();
    
    // 设置 next_queue_handle_ = UINT32_MAX + 1
    // 创建 queue
    // 提交到错误的 va_space（触发 -EINVAL）
    // 捕获日志
    // 验证: 日志显示完整 u64 stream_id（如 "4294967296"）
    // 而不是截断后的 u32（如 "0"）
}
```

### 3.2 `test_h7_issue_1_u64_stress` — 压力测试

**测试场景**:

```cpp
TEST_CASE("H-7 Issue #1 stress: 100K queues with u64 handles") {
    GpgpuDevice device;
    device.initialize();
    
    // 设置 next_queue_handle_ = 0xFFFFFFFF00000000 (高 32-bit 非零)
    // 创建 100K 个 queue
    // 每个 queue 提交 1 次
    // 测量: 总时间, 成功率
    // 预期: 100% 成功率, 无截断错误
}
```

## 4. 跨仓对称性验证

**核心原则**: TaskRunner 端 mock 行为应与 UsrLinuxEmu 端 real 行为**对称**。

**对称性检查表**:

| 行为 | TaskRunner Mock 端 | UsrLinuxEmu Real 端 | 对称性 |
|------|-------------------|--------------------|-----|
| stream_id = UINT32_MAX - 1 | 提交成功 | 提交成功 | ✅ |
| stream_id = UINT32_MAX | 提交成功 | 提交成功 | ✅ |
| stream_id = UINT32_MAX + 1 | 提交成功 | 提交成功 | ✅ |
| stream_id = UINT32_MAX + 1000 | 提交成功 | 提交成功 | ✅ |
| error log 显示完整 u64 | 显示完整值 | 显示完整值 | ✅ |
| R2 mapping 移除后行为一致 | 一致 | 一致 | ✅ |
| 旧 caller 传 stream_id_compat | fallback | fallback | ✅ |

## 5. 实施优先级

### 立即（PR 1 之前可做）

1. **测试设计文档**（本文档）✅
2. **Mock 端测试骨架**（不依赖 UsrLinuxEmu 修复）：
   - `test_h7_issue_1_u64_boundary` 骨架（UINT32_MAX - 1 / UINT32_MAX / UINT32_MAX + 1）
   - `test_h7_issue_1_u64_overflow` 骨架（1000 个 queue 溢出）
   - `test_h7_issue_1_r2_mapping_removed` 骨架（行为一致性）

### PR 1 之后（mock 端 PR 1 改动同步）

3. **PR 1 改动同步到 MockGpuDriver**:
   - MockGpuDriver 的 `submit_batch` 移除 `static_cast<uint64_t>`
   - MockGpuDriver 的 `next_queue_handle_` 允许设置任意 u64 值
4. **Mock 端测试 100% 实施**（包括 UINT32_MAX + 1 验证）
5. **真实端测试**（UsrLinuxEmu owner 端）

## 6. 关联文档

- **openspec change**: `2026-06-26-h3-8-issue-1-coordination/`
- **ADR-034** (UsrLinuxEmu): §Issue #1 canonical
- **tadr-105** (TaskRunner): §Issue #1 mirror
- **AMD ROCm / NVIDIA CUDA 调研**: `docs/shared/research/gpu-queue-id-patterns-2026-06-26.md`
- **ABI 向后兼容测试设计**: `docs/test-fixture/research/abi-backward-compat-test-design-2026-06-26.md`

## 7. 维护

- **Owner**: TaskRunner owner
- **Reviewers**: UsrLinuxEmu owner（跨仓 review）
- **更新时机**: 
  - PR 1 merged 后：更新 §2 实施状态
  - PR 2 merged 后：更新 §5 优先级
- **关联实施**: TaskRunner 测试套件 + UsrLinuxEmu 测试套件