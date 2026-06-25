# ioctl vs mmap 行为等价性测试设计 (2026-06-26)

> **SCOPE**: TEST-FIXTURE  
> **STATUS**: DRAFT  
> **用途**: 为 H-3.7 (ADR-034 Issue #2) 设计 ioctl 路径重构前后的行为等价性验证测试

## 背景

H-3.7 重构 `handlePushbufferSubmitBatch` 从直接调用 `puller_->submitBatch()` 改为通过 `GpuQueueEmu` 委托。虽然 mmap 路径（TADR-006 禁止）不在 H-3 范围内，但**设计时**需确保 ioctl 路径行为等价性，为后续 mmap 路径预留验证基础。

## 测试目标

**核心目标**: 验证重构前后，ioctl path 在相同 input 下产生**完全相同**的 output（fence_id、doorbell、错误码）。

**扩展目标**: 为后续 mmap 路径实施时，提供 ioctl vs mmap 行为等价性验证框架。

## 测试范围

### 测试对象

| 对象 | 测试方式 | 范围 |
|------|---------|------|
| `handlePushbufferSubmitBatch` (ioctl path) | 真实调用 | 重构前后对比 |
| `GpuQueueEmu::submit()` | mock 调用 | 接口验证 |
| `GpuQueueEmu::getQueue()` | mock 调用 | 边界验证 |
| Doorbell 行为 | 监控 | 调用次数和参数 |

### 不在测试范围

- mmap 路径（TADR-006 禁止，不在 H-3 范围）
- 性能基准测试（H-3.7 是功能重构，性能变化 < 1% 可接受）
- 多 GPU 场景（Phase 3 范围）

## 测试用例设计

### TC1: 基本提交等价性（P0）

**目标**: 验证正常提交在重构前后产生相同 fence_id

**Input**:
- `va_space_handle`: valid (created)
- `stream_id`: valid queue attached to VA space
- `entries_addr`: valid GPU memory
- `count`: 1 (single entry)

**Expected Output**:
- `fence_id`: > 0 (same value before and after refactor)
- `doorbell_call_count`: 1
- `error_code`: 0

**Verification**:
- Record pre-refactor `fence_id` for 10 consecutive submits
- Verify post-refactor `fence_id` sequence matches exactly

### TC2: 无效 Queue 提交等价性（P0）

**目标**: 验证无效 queue 提交在重构前后产生相同错误码

**Input**:
- `va_space_handle`: valid
- `stream_id`: not in attached_queues (H-3.6 验证)

**Expected Output**:
- `error_code`: -EINVAL (same as H-3.6 behavior)
- `doorbell_call_count`: 0
- `fence_id`: 0 (not generated)

**Verification**:
- Pre-refactor: returns -EINVAL (H-3.6 bf8192f)
- Post-refactor: returns -EINVAL (via GpuQueueEmu::getQueue() returning null)

### TC3: 不存在 VA Space 提交等价性（P0）

**目标**: 验证无效 VA space 在重构前后产生相同错误码

**Input**:
- `va_space_handle`: invalid (never created or destroyed)

**Expected Output**:
- `error_code`: -EINVAL
- `doorbell_call_count`: 0

**Verification**:
- Pre-refactor: returns -EINVAL (H-3.6 bf8192f)
- Post-refactor: returns -EINVAL (before reaching GpuQueueEmu)

### TC4: Destroyed Queue 提交等价性（P1）

**目标**: 验证已 destroy 的 queue 在重构前后被正确拒绝

**Input**:
- `va_space_handle`: valid
- `stream_id`: queue that was created then destroyed

**Expected Output**:
- `error_code`: -EINVAL (or -ENOENT if Phase 2 error code differentiation)
- `doorbell_call_count`: 0

**Verification**:
- Pre-refactor: depends on `attached_queues` cleanup (H-3.6 may have fixed)
- Post-refactor: `GpuQueueEmu::getQueue()` returns null after destroy

### TC5: 多 Entry 批量提交等价性（P1）

**目标**: 验证批量提交在重构前后产生相同 fence_id 序列

**Input**:
- `count`: 10 (batch of 10 entries)
- Other params: same as TC1

**Expected Output**:
- `fence_id`: 10 consecutive IDs
- `doorbell_call_count`: 1 (single doorbell for batch)

**Verification**:
- Pre-refactor: 10 fence_ids, doorbell_count=1
- Post-refactor: identical sequence

### TC6: Race Condition — Destroy VA Space During Submit（P1）

**目标**: 验证 VA space destroy 与 submit 竞态在重构前后行为一致

**Setup**:
- Thread A: submit batch (delay 100ms inside handler)
- Thread B: destroy VA space after 50ms

**Expected Output**:
- One of: success (A wins) or -EINVAL/-EBUSY (B wins)
- **Consistency**: Pre and post refactor must have same probability distribution

**Verification**:
- Run 100 times, record success/failure ratio
- Pre and post ratios must match (within 10% tolerance)

### TC7: ioctl vs Mock 行为等价性（P1）

**目标**: 验证 TaskRunner 的 MockGpuDriver 与真实 ioctl 行为等价

**Input**: Same as TC1-TC4

**Expected Output**: MockGpuDriver 和 real ioctl produce same results for same inputs

**Verification**:
- Compare `test_gpu_phase2` mock results with `test_gpu_pushbuffer_validation` real results
- Any discrepancy flags MockGpuDriver 偏差（需要修复）

## 测试实现

### 真实测试（test_gpu_pushbuffer_validation 扩展）

```cpp
TEST_CASE("h3_7_tc1: submit equivalence before/after refactor") {
    // Pre-refactor: record fence_id sequence
    // Post-refactor: verify sequence matches
    // Requires: re-run test before and after UsrLinuxEmu code change
}

TEST_CASE("h3_7_tc2: invalid queue submit returns -EINVAL") {
    // H-3.6 already verifies this
    // H-3.7 verifies GpuQueueEmu delegation path produces same error
}

TEST_CASE("h3_7_tc6: race condition destroy va space during submit") {
    // Threaded test with delay injection
    // Verify success/failure ratio consistency
}
```

### Mock 测试（test_gpu_phase2 扩展）

```cpp
TEST_CASE("h3_7_mock: GpuQueueEmu::getQueue invalid handle") {
    auto q = GpuQueueEmu::getQueue(0xDEADBEEF);
    REQUIRE(q == nullptr);  // or throws
}

TEST_CASE("h3_7_mock: GpuQueueEmu::submit valid queue") {
    // MockGpuDriver creates queue
    auto q = GpuQueueEmu::getQueue(queue_handle);
    REQUIRE(q != nullptr);
    auto status = q->submit(entries_addr, 1);
    REQUIRE(status == 0);
}
```

## 行为记录框架

为了验证重构前后行为等价性，需要记录以下指标：

```cpp
struct SubmitBehaviorRecord {
    uint64_t input_va_space_handle;
    uint32_t input_stream_id;
    uint64_t input_entries_addr;
    uint32_t input_count;
    
    int output_error_code;
    uint64_t output_fence_id;
    uint32_t output_doorbell_count;
    
    std::string timestamp;
    std::string git_commit;  // pre or post refactor
};
```

**记录方式**:
- 在 `gpgpu_device.cpp` 的 `handlePushbufferSubmitBatch` 中添加临时日志
- 或在测试框架中拦截 ioctl 调用
- 记录 100 次提交，对比 pre/post 的 fence_id 序列和错误码分布

## 回归验证

重构完成后必须运行的测试：

- [ ] test_gpu_pushbuffer_validation (4 cases) — 必须全部通过
- [ ] test_gpu_phase2 (12 cases) — 必须全部通过
- [ ] test_gpu_plugin — 必须全部通过
- [ ] CLI cuda_queue — 功能正常
- [ ] H-3.7 TC1-TC7 — 等价性验证通过

## 参考

- **ADR-034**: `docs/00_adr/adr-034-h7-deferred-registry.md` §Issue #2
- **TADR-105**: `external/TaskRunner/docs/test-fixture/adr/tadr-105-h7-deferred.md` §H-3.7
- **TADR-006**: `external/TaskRunner/docs/test-fixture/adr/tadr-006-h3-phase2-lifecycle.md` §4
- **GpuQueueEmu**: `plugins/gpu_driver/sim/gpu_queue_emu.h`
- **Test Reference**: `09ae1b0` test_gpu_pushbuffer_validation 4 cases
