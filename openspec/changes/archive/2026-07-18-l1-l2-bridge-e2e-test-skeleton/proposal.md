---
SCOPE: test-fixture
STATUS: IMPLEMENTED
---

# Change: l1-l2-bridge-e2e-test-skeleton

> **Status note**: Code is implemented and on `main`. This change is being archived to capture the design rationale and cross-repo sync record. See `tasks.md` and `design.md` for the full implementation history.

## Why

C-12 KFD multi-file integration 遗留了 E.2.4 跨仓 L1↔L2 bridge 的 deferred 工作。UsrLinuxEmu 端 Phase A（IoctlEntry 表扩展 + 3 个 E2E 测试）已实施完成，验证了：

- `GpgpuDevice IoctlEntry` 表现含 36 条（含 4 个 KFD ioctl：`MAP_MEMORY` / `UNMAP_MEMORY` / `GET_PROCESS_APERTURE` / `UPDATE_QUEUE`）
- VFS → fops->ioctl → GpgpuDevice → kfd_sim_bridge 链路打通
- 并发 kfd_sim_bridge 线程安全已验证

**当前缺口**：TaskRunner 端没有对应测试验证 `GpuDriverClient → UsrLinuxEmu GpgpuDevice → KFD sim` 的完整 L1↔L2 链路。

本 change 在 TaskRunner 端实现 KFD 5 ioctls E2E 测试，与已归档的 `2026-07-12-l1-l2-bridge-e2e-test-skeleton`（聚焦 cuGraphLaunch + cuStreamSynchronize）**形成互补关系**，共同覆盖 L1↔L2 bridge 测试矩阵：

- **本 change（test-fixture scope）**：KFD 5 ioctls 全链路 E2E（MAP/UNMAP_MEMORY + GET_PROCESS_APERTURE + UPDATE_QUEUE + CREATE_QUEUE）
- **已归档 change（umd-evolution scope）**：CUDA Graph E2E（cuGraphLaunch → Puller → fence signal）

## What Changes

### 1. GpuDriverClient 扩展（test-fixture scope）

GpuDriverClient 当前不支持 KFD ioctl 调用。新增 4 个 KFD 方法（public **non-virtual**，仅 `GpuDriverClient` 可见，不上提到 `IGpuDriver`）：

| 方法 | 对应 ioctl | 说明 |
|------|-----------|------|
| `kfd_map_memory(handle, size, out_gpu_va)` | `GPU_IOCTL_MAP_MEMORY` | 映射内存到 GPU 地址空间 |
| `kfd_unmap_memory(handle)` | `GPU_IOCTL_UNMAP_MEMORY` | 从 GPU 解除映射（幂等） |
| `kfd_get_process_aperture(num_nodes, out_apertures)` | `GPU_IOCTL_GET_PROCESS_APERTURE` | 查询进程 GPU 地址孔径 |
| `kfd_update_queue(queue_handle, flags)` | `GPU_IOCTL_UPDATE_QUEUE` | 更新队列属性（`flags=0` = no-op） |

> `GPU_IOCTL_CREATE_QUEUE` 已有封装方法（`GpuDriverClient::create_queue()`），无需新增。
>
> **IGpuDriver 决策（明确）**：本 change **不修改** `IGpuDriver` 接口。KFD ioctl 仅对真实 IOCTL 后端（`GpuDriverClient`）有意义，`CudaStub` / `MockGpuDriver` 不需要模拟它们。如未来 UMD shim 需要通过 `IGpuDriver*` 调用 KFD 方法，应在新的 change 中上提（参见 `design.md` Decision 1 的 future-promotion path）。

### 2. E2E 测试文件

新增 `tests/test_fixture/test_kfd_e2e_bridge.cpp`（213 行）：

- **`RealGpuFixture`**：打开真实 `/dev/gpgpu0` 的 GpuDriverClient fixture（`/dev/gpgpu0` 不可用时 `available_=false`）
- **Test 1**：`kfd_map_memory` / `kfd_unmap_memory` E2E（alloc BO → map → verify → unmap → double-unmap 幂等验证）
- **Test 2**：`kfd_get_process_aperture` + `kfd_update_queue` E2E（create VA+queue → 单/多 node aperture → update_queue no-op → invalid handle error）
- **Test 3**：5 个 KFD ioctls 全链路（create_queue + get_process_aperture + update_queue + map_memory + unmap_memory）
- **SKIP 机制**：使用 `MESSAGE() + return`（doctest 2.4.11 无 `SKIP()` 宏）；ctest 计为 PASS（设备缺失时静默通过，CI 无法区分真测试 vs 跳过，参见 `design.md` Decision 4）

## Capabilities

### New Capabilities

- `kfd-l1-l2-bridge-e2e`: TaskRunner 端 KFD 5 ioctls L1↔L2 bridge E2E 测试

## Impact

- **新增文件**：
  - `tests/test_fixture/test_kfd_e2e_bridge.cpp` (213 行)
- **修改文件**：
  - `include/test_fixture/gpu_driver_client.h`（新增 4 个 KFD 方法，行 831-866）
  - `cmake/TestFixture.cmake`（注册新测试目标，行 62-72）
- **不涉及**：`test-fixture` / `umd` / `shared` 其他文件
- **不修改 `IGpuDriver` 接口**：KFD 方法保持 GpuDriverClient-only（参见 `design.md` Decision 1）
- **跨仓同步**：UsrLinuxEmu 端 Phase A 已完成（IoctlEntry 表扩展 + 3 测试）；TaskRunner commit 在 UsrLinuxEmu commit 之前（ADR-035 §R5.1）

## Acceptance Criteria

- [x] `gpu_driver_client.h` 新增 4 个 KFD 方法（`kfd_map_memory` / `kfd_unmap_memory` / `kfd_get_process_aperture` / `kfd_update_queue`）
- [x] `test_kfd_e2e_bridge` 编译通过，3 个 TEST_CASE
- [x] `/dev/gpgpu0` 未加载时静默通过（MESSAGE+return），已加载时 PASS
- [x] TaskRunner test-fixture ctest 4/4 PASS（无回归；3 新 KFD + 1 既有 `test_gpu_phase2`）
- [x] `tools/docs-audit.sh` PASS
- [x] 跨仓 submodule bump 已完成（UsrLinuxEmu 端 Phase A 104/104 ctest + TaskRunner 4/4 双绿）

## Risk

- **fd 暴露（已缓解）**：KFD 方法是 public 成员方法，但封装了 `ioctl(fd_, ...)` 调用，`fd_` 仍为 private。实际实现中 proposal 原始担心的"暴露 fd_"风险**未发生**（参见 `design.md` Decision 2）。
- **`handle` u32 截断**：当前 `gpu_map_memory_args.handle` 为 `u32`（gpu_ioctl.h:396），`bo_handle` 是 `uint64_t`，测试用 `static_cast<uint32_t>(bo_handle)` 显式截断。当前安全（handle 不会超过 u32），但若未来 UsrLinuxEmu 将 handle 拓宽为 u64，会静默截断产生错误映射（参见 `design.md` Decision 5）。
- **`apertures_ptr` reinterpret_cast**：用户指针到 `u64` 的转换在 64 位 Linux 安全；缺少 `static_assert(sizeof(void*) <= sizeof(uint64_t))` 保护。
- **`queue_flags=0` 是 no-op**：`gpu_update_queue_args` 含 6 字段（`queue_handle`, `ring_base_address`, `ring_size`, `queue_percent`, `queue_priority`, `queue_flags`），本 change 仅设置 2 个，其余零初始化。`flags=0` 表示"不更新任何属性"，ioctl handler 直接返回 0——**测试断言 `ret==0` 通过但验证力度极弱**（参见 `design.md` Decision 3）。
- **测试断言过度依赖 UsrLinuxEmu 内部**：`gpu_id == 0` / `gpu_id == 3` 假设 sim 的 gpu_id 分配策略固定；若 sim 修改策略，Test 2 会失败（参见 `tasks.md` Test 2 coupling note）。
- **跨仓时序**：TaskRunner commit 在 UsrLinuxEmu commit **之前**（ADR-035 §R5.1），代码开发可并行。

## Cross-repo Dependency

- **UsrLinuxEmu**: Phase A 已完成（`openspec/changes/2026-07-16-kfd-l1-l2-bridge-e2e/`）
  - `GpgpuDevice IoctlEntry` 表扩展：4 个 KFD ioctl 已加入
  - `kfd_sim_reset()` 在 plugin init 中调用
  - 104/104 ctest PASS, docs-audit 43/43 PASS
- **同步协议**: ADR-035 §Rule 5.1 4 步（TaskRunner commit 先于 UsrLinuxEmu commit），本 change 已完成步骤 1（TaskRunner commit），步骤 2-4（submodule bump + UsrLinuxEmu commit + ctest 双绿）已在 `main` 上验证。