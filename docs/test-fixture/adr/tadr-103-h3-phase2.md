---
SCOPE: TEST-FIXTURE
STATUS: ACCEPTED
REPLACES: tadr-006
---

# TADR-006: H-3 Phase 2 Lifecycle Consumer-Lens

**状态**: ✅ Accepted
**日期**: 2026-06-23
**提案人**: TaskRunner owner 协同
**评审者**: UsrLinuxEmu Architecture Team + TaskRunner owner
**关联 ADR (UsrLinuxEmu)**: [ADR-033](../../../../docs/00_adr/adr-033-h3-phase2-lifecycle.md) (canonical)
**关联 Change**: `openspec/changes/archive/2026-06-22-h3-phase2-management/`
**关联 Source**: `openspec/changes/archive/2026-06-22-h3-phase2-management/design.md` §D1-D5 + R2

---

## Context

H-3 在 IGpuDriver 接口新增 5 个 Phase 2 方法（VA Space 创建/销毁 + GPU 绑定 + Queue 创建/销毁），由 `GpuDriverClient` 和 `CudaStub` 各实现。上游决策由 UsrLinuxEmu ADR-033 记录（见 §Decision D1-D5）。

本 TADR 记录 **TaskRunner 侧的具体实现选择**（sentinel guard / 零初始化 ioctl args / errno 日志 / R2 mapping 显式化），不重复 ADR-033 的上游决策文本。

## Decision

### 5 Phase 2 方法签名（与 IGpuDriver 接口一致）

| 方法 | 返回 | 失败语义 |
|------|------|---------|
| `create_va_space(uint32_t flags)` | `uint64_t` | 0 = 失败；≥1 = 成功 |
| `destroy_va_space(uint64_t va_space_handle)` | `int` | 0 = 成功；-1 = 失败 |
| `register_gpu(uint64_t va_space_handle, uint32_t gpu_id, uint32_t flags)` | `int` | 0 = 成功；-1 = 失败 |
| `create_queue(uint64_t va_space_handle, uint32_t queue_type, uint32_t priority, uint64_t ring_buffer_size)` | `uint64_t` | 0 = 失败；≥1 = 成功 |
| `destroy_queue(uint64_t queue_handle)` | `int` | 0 = 成功；-1 = 失败 |

### 4 项 Consumer-Lens 实施细节

#### 1. Sentinel guard 一致性

所有 handle==0 的 destroy / register / create_queue 调用走 guard：

```cpp
// gpu_driver_client.h L455-457
int destroy_va_space(uint64_t va_space_handle) override {
    if (!is_open()) return -1;
    if (va_space_handle == 0) return -1;  // 守卫：拒绝 sentinel
    // ...
}
```

#### 2. 零初始化 ioctl args

```cpp
// gpu_driver_client.h L437
struct gpu_va_space_args args = {};  // 全部字段零初始化（含 va_space_handle 输出字段）
```

避免未初始化字段被 ioctl 误读，与 H-1 `gpu_pushbuffer_args` 一致。

#### 3. errno 日志（ioctl 失败时）

```cpp
// gpu_driver_client.h L441-444
if (ioctl(fd_, GPU_IOCTL_CREATE_VA_SPACE, &args) < 0) {
    std::cerr << "GpuDriverClient: GPU_IOCTL_CREATE_VA_SPACE failed"
              << " (errno=" << errno << ")\n";
    return 0;
}
```

所有 5 个方法的 ioctl 失败路径都含 errno 日志，便于生产诊断。

#### 4. 业务校验（仅 create_queue）

```cpp
// gpu_driver_client.h L511-520
if (priority > 100) {
    std::cerr << "[GpuDriverClient] create_queue: invalid priority " << priority
              << " (valid range: 0-100)" << std::endl;
    return 0;
}
if (ring_buffer_size == 0) {
    std::cerr << "[GpuDriverClient] create_queue: invalid ring_buffer_size 0" << std::endl;
    return 0;
}
```

priority 范围 0-100 校验（GPU 设备 spec）+ ring_buffer_size > 0 校验。

## Consumer-Lens

### 实施 commit 链（TaskRunner 仓 9 commits）

```
241f3ed feat(igpu): implement 5 Phase 2 methods on GpuDriverClient (H-3)
25e370d refactor(igpu): move doorbell comment before return in create_queue (H-3 review)
9a5b68e feat(igpu): implement 5 Phase 2 mock methods on CudaStub (H-3)
6aec021 fix(igpu): add va_space_handle==0 guard to CudaStub::register_gpu (H-3 review)
0a7b59e test(igpu): add test_gpu_phase2.cpp with 10 H-3 doctest cases + 2 R2 bonus
84455ed test(igpu): clarify T6 inject_error intent (H-3 review)
e292831 feat(cli): add cuda_va_space + cuda_queue subcommands for H-3 Phase 2
8625b82 refactor(cli): make R2 mapping truncation explicit in cuda_queue (H-3 review)
```

### 测试覆盖

- ✅ `tests/test_gpu_phase2.cpp` 12 doctest cases（5 success + 4 mock-behavior + 1 R2 mapping + 2 R2 violation）
- ✅ H-1 baseline preserved：`test_cuda_scheduler` 8/8
- ✅ H-2.5 baseline preserved：`test_gpu_architecture` 10/11

### CLI 集成

- ✅ `cuda_va_space create <flags>` / `cuda_va_space destroy <handle>`
- ✅ `cuda_queue create <va_space> <type> <prio> <ring>` / `cuda_queue destroy <handle>`
- ✅ `print_cuda_help()` 含 6 个命令的用法（`cmd_cuda.cpp:36-57`）

### CudaStub mock 实现细节

- `next_va_space_handle_` + `next_queue_handle_` atomic 单调 from 1
- `va_space_map_` + `queue_map_` existence tracking（用于 destroy 校验）
- `mock_state_mutex_` 保护 map

## H-3.5 Completion（2026-06-25）

H-3.5 follow-up 闭环：3 项遗留问题已 shippable。

### 1. test_gpu_architecture 回归修复

`tests/test_fixture/test_gpu_architecture.cpp:207-214` 的 `CHECK_THROWS` 期望与 H-3 spec L14-29 + L52-57 + L70-73 + L93-96 + L120-123 矛盾（spec 明确 handle==0 返回 0/-1，**不**抛异常）。已更新为 `CHECK_NOTHROW + CHECK(result == 0/-1)`，测试名改为 `H-3.5: GpuDriverClient guards return 0/-1, no throw`。11/11 PASS。

### 2. CudaScheduler 抽象泄漏修复

`src/test_fixture/cuda_scheduler.cpp:45, 65` 共 2 处 `dynamic_cast<CudaStub*>(driver_)` 已删除。改用 IGpuDriver 抽象调用：
- `driver_->set_stub_mode(stub_mode)` + `driver_->initialize()`
- `driver_->shutdown()`

4 处 legacy dynamic_cast（line 101, 147, 188, 227, 269，对应 `mem_alloc` / `memcpy_*` / `launch_kernel`）**保留**（这些是 CudaStub-only CUDA Driver API 路径，不适合上移到 IGpuDriver 抽象）。

IGpuDriver 接口扩展 3 个 lifecycle 方法（28 → 31）：
- `set_stub_mode(bool)` — 默认 no-op（CudaStub override，GpuDriverClient/MockGpuDriver 用默认）
- `initialize()` — 默认返回 0（CudaStub override 返回真实初始化结果）
- `shutdown()` — 默认 no-op（CudaStub override 清理 Event）

### 3. MockGpuDriver guard 偏差修复

`tests/test_fixture/mock_gpu_driver.hpp` 的 5 个 Phase 2 方法添加 guards（与 GpuDriverClient/CudaStub 行为一致）：
- `create_va_space` → 检查 `is_open_`（新增状态跟踪）
- `destroy_va_space(0)` → 返回 -1
- `register_gpu(0, ...)` → 返回 -1
- `create_queue(0, ...)` → 返回 0
- `destroy_queue(0)` → 返回 -1

`tests/test_fixture/test_gpu_phase2.cpp:115-195` 的 T6-T9 已更新，**真正**验证 guard rejection 而非 mock canned value。12/12 PASS。

### 关联文件 / Commit 链

- `include/shared/igpu_driver.hpp:305-338` — 3 lifecycle 方法
- `include/test_fixture/cuda_stub.hpp` — `set_stub_mode/initialize/shutdown` 加 override
- `include/test_fixture/cuda_stub.cpp:31-43` — `initialize()` 返回 int
- `tests/test_fixture/mock_gpu_driver.hpp` — is_open_ tracking + 5 guards + 3 lifecycle
- `src/test_fixture/cuda_scheduler.cpp:35-50, 62-65` — 删除 2 dynamic_cast
- `tests/test_fixture/test_gpu_architecture.cpp:203-232` — 回归修复
- `tests/test_fixture/test_gpu_phase2.cpp:112-167` — T6-T9 更新

### 关联 Change

`openspec/changes/archive/2026-06-25-h3-5-followup-test-fixture-cleanup/`

### 关联 TADR

`docs/test-fixture/adr/tadr-109-igpu-driver-uniform-scheduling.md`（跟踪 IGpuDriver 接口扩展 3 方法决策）

## Consequences

### 正面

- ✅ Phase 2 lifecycle API 完整（5 方法覆盖 VA Space + Queue）
- ✅ Sentinel guard + errno 日志 + 业务校验 三重保护
- ✅ 12/12 doctest cases pass + CLI smoke test 通过
- ✅ test_gpu_architecture 回归修复 11/11 PASS
- ✅ CudaScheduler 抽象泄漏修复（2/6 dynamic_cast 删除）
- ✅ MockGpuDriver guard 与 GpuDriverClient/CudaStub 一致

### 负面 / 风险

- ⚠️ 4 处 legacy dynamic_cast 保留（mem_alloc/memcpy_*/launch_kernel），待未来 H-3.6+ 决定是否上移到 IGpuDriver
- ⚠️ CLI top-level `--help` 不更新（`cmd_buffer_v2.cpp` out of scope，`cuda_help` 子命令正常工作）

## 跨引用

- **上游 UsrLinuxEmu ADR-033**: §Decision D1-D5 + §R2 Mapping Contract
- **关联 TADR**: TADR-005 (IGpuDriver 抽象层), TADR-007 (R2 mapping 显式化), TADR-008 (H-7 上游 issue)
- **关联文件**: `include/gpu_driver_client.h:435-551`, `src/cuda_stub.cpp:414-506`, `src/cmd_cuda.cpp:253-381`

---

**最后更新**: 2026-06-23（H-4.5 docs governance cleanup 建立 TADR-006 consumer-lens）
