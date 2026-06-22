# Tasks: h3-phase2-management

> **依赖**: proposal ✅ / design ✅ (D1-D5 FINALIZED) / specs ✅
> **预估总工时**: 4-6 小时（含跨仓 sync + 验证）
> **前置条件**: **H-2.5** `h2-5-architecture-foundation` 已完成（提供 `IGpuDriver` / `MockGpuDriver` / DI）

## 1. 前置检查：H-2.5 完成性

- [ ] **1.1** 验证 `openspec/changes/archive/h2-5-architecture-foundation/` 存在
- [ ] **1.2** 验证 `include/igpu_driver.hpp` 已定义 5 个 Phase 2 纯虚方法（`create_va_space` 等）
- [ ] **1.3** 验证 `tests/mock_gpu_driver.hpp` 提供 `MockGpuDriver`（H-3 测试依赖）
- [ ] **1.4** 验证 `CudaScheduler` 构造函数接受 `IGpuDriver*` 注入
- [ ] **1.5** 验证 `cli_main.cpp` 修复了 `init_gpu_client()` 死调用
- [ ] **1.6** **任何 1.1-1.5 未通过 → 立即停止 H-3，先完成 H-2.5**

## 2. TaskRunner 实现：`GpuDriverClient` 5 个方法

- [ ] **2.1** 进入 TaskRunner：`cd external/TaskRunner`
- [ ] **2.2** 在 `include/gpu_driver_client.h` / `src/gpu_driver_client.cpp` 实现 `create_va_space(uint32_t flags) → u64`
  - 完整代码参见 `design.md §5 Method Signatures`
  - guard：`!is_open()` → return 0；`ioctl` 失败 → return 0 + cerr
- [ ] **2.3** 实现 `destroy_va_space(uint64_t handle) → int`
  - guard：`handle == 0` → return -1（无 ioctl）
- [ ] **2.4** 实现 `register_gpu(uint64_t va_space_handle, uint32_t gpu_id, uint32_t flags) → int`
  - guard：`va_space_handle == 0` → return -1（H-1 sentinel）
- [ ] **2.5** 实现 `create_queue(uint64_t va_space_handle, uint32_t queue_type, uint32_t priority, uint64_t ring_buffer_size) → u64`
  - 4 输入字段完整（va_space, type, priority, ring_size）
  - guard：`va_space_handle == 0` → return 0；`priority > 100` → return 0
  - 返回 `args.queue_handle`（u64, monotonic from 1 per R2）
- [ ] **2.6** 实现 `destroy_queue(uint64_t queue_handle) → int`
  - guard：`queue_handle == 0` → return -1
- [ ] **2.7** 确认所有方法用 `struct X args = {};` 零初始化（与 H-1 一致）
- [ ] **2.8** 确认 `current_va_space_handle_` 字段保留（H-1 兼容），**不**在 `create_va_space()` 中自动 set（D1 决策）

## 3. TaskRunner 实现：`CudaStub` 5 个 mock 方法

- [ ] **3.1** 在 `src/cuda_stub.cpp` 实现 5 个 mock 方法（与 `GpuDriverClient` 同签名）
- [ ] **3.2** 内部维护 `std::atomic<uint64_t> next_va_space_handle_{1}` 与 `next_queue_handle_{1}`（monotonic）
- [ ] **3.3** `create_va_space(flags)` → `next_va_space_handle_++`，记录调用到 `std::cerr`（调试用）
- [ ] **3.4** `create_queue(...)` → `next_queue_handle_++`，同上
- [ ] **3.5** `destroy_*` 方法从内部 `std::unordered_map<handle, ...>` 删除（mock 资源管理）
- [ ] **3.6** 确认所有 mock 方法**不**调用任何 ioctl（纯 in-memory 状态机）

## 4. 测试夹具：`MockGpuDriver`（H-2.5 提供，H-3 验证）

- [ ] **4.1** 确认 `MockGpuDriver` 暴露 5 个 Phase 2 方法（H-2.5 范围）
- [ ] **4.2** 确认 `MockGpuDriver` 内部 `std::vector<CallRecord>` 记录每次调用（method name + args）
- [ ] **4.3** 确认 `MockGpuDriver` 可注入错误（`inject_create_va_space_error` 等）→ 单元测试错误路径

## 5. 测试实现：`tests/test_gpu_phase2.cpp`

- [ ] **5.1** 新建 `tests/test_gpu_phase2.cpp`（10 个 doctest case）
- [ ] **5.2** 注入 `MockGpuDriver` 到 fixture：
  ```cpp
  class Phase2Fixture {
  protected:
    MockGpuDriver mock;
    CudaScheduler scheduler{&mock};  // DI
  };
  ```
- [ ] **5.3** Test 1-5（成功路径）：
  - `create_va_space_returns_nonzero_handle` → mock 返回 1 → scheduler 收到 1
  - `destroy_va_space_succeeds_with_valid_handle` → mock 返回 0
  - `register_gpu_succeeds_with_valid_va_space` → mock 返回 0
  - `create_queue_returns_u64_handle` → mock 返回 >= 1 的 u64
  - `destroy_queue_succeeds_with_valid_handle` → mock 返回 0
- [ ] **5.4** Test 6-9（guard / 错误路径）：
  - `create_va_space_guard_when_closed` → `mock.is_open()=false` → 0
  - `destroy_va_space_guard_when_handle_zero` → handle=0 → -1（无 mock 调用）
  - `register_gpu_guard_when_va_space_zero` → va_space=0 → -1（无 mock 调用）
  - `create_queue_guard_when_va_space_zero` → va_space=0 → 0（无 mock 调用）
- [ ] **5.5** Test 10（R2 mapping 契约）：
  - `r2_mapping_stream_id_equals_low32_of_queue_handle`
  - 流程：mock 返回 queue_handle=0x100000001 → scheduler 保存 → submit_batch 时填 `stream_id = (uint32_t)0x100000001 = 1` → 验证 mock.submit_batch_args.stream_id == 1
- [ ] **5.6** 在 `CMakeLists.txt` 注册新测试 target：
  ```cmake
  add_executable(test_gpu_phase2 tests/test_gpu_phase2.cpp ...)
  add_test(NAME test_gpu_phase2 COMMAND test_gpu_phase2)
  ```
- [ ] **5.7** 本地构建：`cd build && make -j4`
- [ ] **5.8** 运行测试：`./test_gpu_phase2` — 预期 10/10 cases pass
- [ ] **5.9** 回归既有测试：`./test_cuda_scheduler` — 预期 8/8 cases 仍 pass（H-1 路径无影响）

## 6. CLI 集成：`src/cmd_cuda.cpp`

- [ ] **6.1** 在 `cmd_cuda.cpp` 添加 `cmd_cuda_va_space(int argc, char** argv)`：
  - `cuda_va_space create <flags>` → `g_gpu_client->create_va_space(flags)` → `printf("va_space_handle=%lu\n", handle)`
  - `cuda_va_space destroy <handle>` → `g_gpu_client->destroy_va_space(handle)`
- [ ] **6.2** 添加 `cmd_cuda_queue(int argc, char** argv)`：
  - `cuda_queue create <va_space> <type> <priority> <ring_size>` → `create_queue(...)` → 打印 queue_handle
  - `cuda_queue destroy <handle>` → `destroy_queue(...)`
- [ ] **6.3** 在 `main()` dispatch 表注册 2 个 subcommand
- [ ] **6.4** 验证 `cli_main.cpp` 的 `init_gpu_client()` 始终被调用（H-2.5 修复后无 dead code）
- [ ] **6.5** 手动跑：`./build/taskrunner cuda_va_space create 0` → 打印非零 handle
- [ ] **6.6** 手动跑：`./build/taskrunner cuda_queue create <handle> 0 50 4096` → 打印非零 queue_handle

## 7. 跨仓同步（UsrLinuxEmu）

- [ ] **7.1** TaskRunner 仓 commit + push：
  ```bash
  cd external/TaskRunner
  git add -A
  git commit -m "feat(igpu): Phase 2 VA Space + Queue lifecycle (H-3)"
  git push -u origin feat/h3-phase2-wrappers
  ```
- [ ] **7.2** 进入 UsrLinuxEmu：`cd /workspace/project/UsrLinuxEmu`
- [ ] **7.3** 更新子模块指针：`git add external/TaskRunner`
- [ ] **7.4** 编辑 `plans/sync-plan.md` line 247-249：S5 标记 "✅ 已完成"
- [ ] **7.5** 编辑 `AGENTS.md` "Phase 1.5 进度" section 加 S5 ✅ Phase 2 管理 API
- [ ] **7.6** 激活 openspec change：
  ```bash
  mv /workspace/project/UsrLinuxEmu/external/TaskRunner/plans/2026-06-19-h3-phase2-openspec-skeleton \
     /workspace/project/UsrLinuxEmu/openspec/changes/h3-phase2-management
  ```
  激活后：移除 README.md 中 ⚠️ DRAFT 标记 + 更新 `.openspec.yaml` 的 `status: DRAFT` → `status: ACTIVE`
- [ ] **7.7** 把 openspec change 纳入 git：`git add openspec/changes/h3-phase2-management/`
- [ ] **7.8** 验证文件：`git ls-tree HEAD openspec/changes/h3-phase2-management/` — 6 files tracked
- [ ] **7.9** UsrLinuxEmu 仓 commit：
  ```bash
  git commit -m "feat(h3): H-3 cross-repo sync + openspec change tracking"
  ```

## 8. 验证 + 归档

- [ ] **8.1** UsrLinuxEmu build：`make -j4` 100%
- [ ] **8.2** ctest：所有 tests pass（`test_gpu_plugin` + `test_cuda_scheduler` 8/8 + `test_gpu_phase2` 10/10）
- [ ] **8.3** docs-audit：`bash tools/docs-audit.sh --strict`
- [ ] **8.4** TaskRunner 子模块独立跑通：
  ```bash
  cd external/TaskRunner && cd build && make -j4
  ./test_cuda_scheduler   # 8/8
  ./test_gpu_phase2       # 10/10
  ```
- [ ] **8.5** 跑 openspec 归档：`openspec archive h3-phase2-management`
- [ ] **8.6** 验证归档目录：`openspec/changes/archive/2026-06-19-h3-phase2-management/`
- [ ] **8.7** `openspec list` 期望 "No active changes"
- [ ] **8.8** archive 目录纳入 git：
  ```bash
  git add openspec/changes/archive/2026-06-19-h3-phase2-management/
  git commit -m "chore(openspec): archive h3-phase2-management"
  git push
  ```

## 回滚预案

| 阶段 | 回滚命令 |
|------|---------|
| §2 / §3 / §5 失败 | `git reset HEAD~1 && git restore .` (TaskRunner 仓，未 push) |
| §7.1 push 后 | `git push --force-with-lease` 回退 + revert PR |
| §7.9 UsrLinuxEmu commit | `git restore --staged openspec/changes/ external/TaskRunner` |
| §8 archive | `rm -rf openspec/changes/archive/2026-06-19-h3-phase2-management/` |

各阶段独立可逆。
