# Tasks: phase3-1-igpu-driver-extension

> **状态**: 🔄 PROPOSED（2026-07-05，TaskRunner owner 起草，等待启动）
> **依赖**: UsrLinuxEmu `2026-07-05-sim-stream-primitive-support/` 已 ✅ ACCEPTED（无前置 commit 阻塞）
> **约束**: 现有 76+ 测试全过；零 `GPU_IOCTL_*` #define 引用；30 min 总实施时长

---

## 前置条件（验证基线）

- [ ] **0.1** 确认 TaskRunner HEAD 是 `ba16139`：
  ```bash
  cd /workspace/project/UsrLinuxEmu/external/TaskRunner
  git log --oneline -1
  # 预期: ba16139 feat(tools): add verify-phase17.sh ...
  ```
- [ ] **0.2** 确认当前 IGpuDriver 方法数 = 31（不含析构）：
  ```bash
  grep -c "^    virtual " include/shared/igpu_driver.hpp
  # 预期: 32 (31 + 1 析构)
  ```
- [ ] **0.3** 确认基线构建 + 测试通过：
  ```bash
  cd build && cmake --build . -j4
  for t in test_cuda_scheduler test_gpu_architecture test_gpu_phase2 \
            test_cuda_runtime_api test_cuda_shim; do
    ./$t 2>&1 | tail -1
  done
  # 预期: 76/76+ PASS
  ```
- [ ] **0.4** 确认无 Phase 3 方法已存在（避免重复添加）：
  ```bash
  grep -E "stream_capture_|graph_create|graph_destroy|mem_pool_" include/shared/igpu_driver.hpp
  # 预期: 无匹配
  ```

---

## Workstream: IGpuDriver 接口扩展 ⏱ 30 min

> **Commit**: `feat(igpu_driver): 15-method no-op extension for Phase 3.1/3.2 (per 4-step coordination Step 1)`
>
> **影响文件**: 仅 `include/shared/igpu_driver.hpp`（无其他文件修改）

### Phase 3.1 Stream Capture + Graph（10 方法）

- [ ] **T1.1** 在 `igpu_driver.hpp` 添加 section 注释：
  ```cpp
  // ============================================================
  // Phase 3.1 Stream Capture / Graph (10, 虚函数 默认 no-op, 非纯虚)
  // ============================================================
  //
  // 决策来源（UsrLinuxEmu Architecture Team 2026-07-05 反馈）：
  // - F-1 capture mode 仅接受 GLOBAL
  // - B-3 fence_id 范围划分（HAL [1, 1<<32-1] + sim [1<<32, INT64_MAX]）
  // - F-3 kernargs_bo_handle=0 表示无 kernargs BO
  // - F-4 int64_t 返回约定（<0 = errno, >= (1<<32) = valid fence_id）
  ```

- [ ] **T1.2** 添加 `stream_capture_status` 方法：
  ```cpp
  /**
   * @brief Query stream capture status (Phase 3.1)
   * @param stream_id 流 ID
   * @param[out] status_out SIM_STREAM_CAPTURE_STATUS_* (NONE/ACTIVE/INVALID)
   * @return 0 成功, -1 失败
   */
  virtual int stream_capture_status(uint32_t stream_id, uint32_t* status_out) { return -1; }
  ```

- [ ] **T1.3** 添加 `stream_capture_begin` 方法（含 F-1 注释）：
  ```cpp
  /**
   * @brief Begin stream capture (Phase 3.1)
   * @param stream_id 流 ID
   * @param mode capture mode (SIM_CAPTURE_MODE_GLOBAL = 0 only; other modes return -EINVAL)
   * @return 0 成功, -1 失败
   */
  virtual int stream_capture_begin(uint32_t stream_id, uint32_t mode) { return -1; }
  ```

- [ ] **T1.4** 添加 `stream_capture_end` 方法：
  ```cpp
  /**
   * @brief End stream capture, return graph handle (Phase 3.1)
   * @param stream_id 流 ID
   * @param[out] graph_handle_out 返回 graph handle (>=1)
   * @return 0 成功, -1 失败
   */
  virtual int stream_capture_end(uint32_t stream_id, uint64_t* graph_handle_out) { return -1; }
  ```

- [ ] **T1.5** 添加 `graph_create` 方法：
  ```cpp
  /**
   * @brief Create empty CUDA graph (Phase 3.1)
   * @param[out] graph_handle_out graph handle (>=1)
   * @return 0 成功, -1 失败
   */
  virtual int graph_create(uint64_t* graph_handle_out) { return -1; }
  ```

- [ ] **T1.6** 添加 `graph_destroy` 方法：
  ```cpp
  /**
   * @brief Destroy CUDA graph (Phase 3.1)
   * @param graph_handle graph handle
   * @return 0 成功, -1 失败
   */
  virtual int graph_destroy(uint64_t graph_handle) { return -1; }
  ```

- [ ] **T1.7** 添加 `graph_add_kernel_node` 方法（含 F-3 注释）：
  ```cpp
  /**
   * @brief Add kernel-launch node to graph (Phase 3.1)
   * @param graph_handle graph handle
   * @param kernel_index kernel registry index
   * @param grid_x/y/z, block_x/y/z launch dims
   * @param kernargs_bo_handle BO handle (0 = no kernargs, no validation)
   * @return 0 成功, -1 失败
   */
  virtual int graph_add_kernel_node(uint64_t graph_handle, uint32_t kernel_index,
                                     uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
                                     uint32_t block_x, uint32_t block_y, uint32_t block_z,
                                     uint64_t kernargs_bo_handle) { return -1; }
  ```

- [ ] **T1.8** 添加 `graph_add_memcpy_node` 方法：
  ```cpp
  /**
   * @brief Add memcpy node to graph (Phase 3.1)
   * @param graph_handle graph handle
   * @param src_va source virtual address
   * @param dst_va destination virtual address
   * @param size bytes to copy
   * @param is_h2d 1=H2D, 0=D2H
   * @return 0 成功, -1 失败
   */
  virtual int graph_add_memcpy_node(uint64_t graph_handle,
                                     uint64_t src_va, uint64_t dst_va,
                                     uint64_t size, uint32_t is_h2d) { return -1; }
  ```

- [ ] **T1.9** 添加 `graph_instantiate` 方法：
  ```cpp
  /**
   * @brief Instantiate graph into executable (Phase 3.1)
   * @param graph_handle graph handle
   * @param[out] exec_handle_out executable handle (>=1)
   * @return 0 成功, -1 失败
   */
  virtual int graph_instantiate(uint64_t graph_handle, uint64_t* exec_handle_out) { return -1; }
  ```

- [ ] **T1.10** 添加 `submit_graph` 方法（**int64_t** — B-3 + F-4）：
  ```cpp
  /**
   * @brief Launch instantiated graph (Phase 3.1)
   * @param graph_exec_handle executable graph handle
   * @param stream_id 流 ID
   * @return fence_id (>= 1<<32 = valid), 0 = no fence, <0 = negative errno, -1 = not implemented
   */
  virtual int64_t submit_graph(uint64_t graph_exec_handle, uint32_t stream_id) { return -1; }
  ```

- [ ] **T1.11** 添加 `destroy_graph_exec` 方法：
  ```cpp
  /**
   * @brief Destroy graph executable (Phase 3.1)
   * @param graph_exec_handle executable graph handle
   * @return 0 成功, -1 失败
   */
  virtual int destroy_graph_exec(uint64_t graph_exec_handle) { return -1; }
  ```

### Phase 3.2 Memory Pool（5 方法）

- [ ] **T1.12** 添加 section 注释：
  ```cpp
  // ============================================================
  // Phase 3.2 Memory Pool (5, 虚函数 默认 no-op, 非纯虚)
  // ============================================================
  //
  // 决策来源（UsrLinuxEmu Architecture Team 2026-07-05 反馈）：
  // - B-2 Pool VA 范围采用 Option B（VA 子范围预留）
  // - F-4 int64_t 返回约定
  ```

- [ ] **T1.13** 添加 `mem_pool_create` 方法（含 B-2 注释）：
  ```cpp
  /**
   * @brief Create memory pool (Phase 3.2)
   * @param va_space_handle owning VA Space handle
   * @param size pool total size in bytes (reserves VA sub-range per Option B)
   * @param flags pool flags (CU_MEMPOOL_*)
   * @param[out] pool_handle_out pool handle (>=1)
   * @return 0 成功, -1 失败
   */
  virtual int mem_pool_create(uint64_t va_space_handle, uint64_t size,
                               uint32_t flags, uint64_t* pool_handle_out) { return -1; }
  ```

- [ ] **T1.14** 添加 `mem_pool_destroy` 方法：
  ```cpp
  /**
   * @brief Destroy memory pool (Phase 3.2)
   * @param pool_handle pool handle
   * @return 0 成功, -1 失败
   */
  virtual int mem_pool_destroy(uint64_t pool_handle) { return -1; }
  ```

- [ ] **T1.15** 添加 `mem_pool_alloc` 方法：
  ```cpp
  /**
   * @brief Synchronous allocation from pool (Phase 3.2)
   * @param pool_handle pool handle
   * @param size allocation size in bytes
   * @param[out] va_out returned virtual address
   * @return 0 成功, -1 失败
   */
  virtual int mem_pool_alloc(uint64_t pool_handle, uint64_t size,
                              uint64_t* va_out) { return -1; }
  ```

- [ ] **T1.16** 添加 `mem_pool_alloc_async` 方法（**int64_t** — B-3 + F-4）：
  ```cpp
  /**
   * @brief Asynchronous allocation from pool (Phase 3.2)
   * @param pool_handle pool handle
   * @param size allocation size in bytes
   * @param stream_id target stream
   * @param[out] va_out returned virtual address
   * @return fence_id (>= 1<<32 = valid), 0 = no fence, <0 = negative errno, -1 = not implemented
   */
  virtual int64_t mem_pool_alloc_async(uint64_t pool_handle, uint64_t size,
                                        uint32_t stream_id, uint64_t* va_out) { return -1; }
  ```

- [ ] **T1.17** 添加 `mem_pool_free_async` 方法（**int64_t** — B-3 + F-4）：
  ```cpp
  /**
   * @brief Asynchronous free (Phase 3.2)
   * @param va virtual address to free
   * @param stream_id target stream
   * @return fence_id (>= 1<<32 = valid), 0 = no fence, <0 = negative errno, -1 = not implemented
   */
  virtual int64_t mem_pool_free_async(uint64_t va, uint32_t stream_id) { return -1; }
  ```

---

## 验证步骤

- [ ] **T2.1** 编译验证（无 warning）：
  ```bash
  cd /workspace/project/UsrLinuxEmu/external/TaskRunner
  cmake --build build -j4 2>&1 | tee /tmp/build.log
  grep -i "warning" /tmp/build.log
  # 预期: 无新增 warning
  ```

- [ ] **T2.2** 跑现有 76+ 测试（向后兼容验证）：
  ```bash
  cd build
  for t in test_cuda_scheduler test_gpu_architecture test_gpu_phase2 \
            test_cuda_runtime_api test_cuda_shim; do
    ./$t 2>&1 | tail -1
  done
  # 预期: 全部 PASS
  ```

- [ ] **T2.3** 验证 IGpuDriver 方法数 = 46：
  ```bash
  grep -c "^    virtual " include/shared/igpu_driver.hpp
  # 预期: 47 (46 + 1 析构)
  ```

- [ ] **T2.4** 验证新方法全部**非纯虚**：
  ```bash
  grep -A1 "Phase 3" include/shared/igpu_driver.hpp | grep "= 0;"
  # 预期: 无匹配（所有新方法都用 { return -1; } 默认实现）
  ```

- [ ] **T2.5** 验证 3 个 int64_t 方法：
  ```bash
  grep -E "virtual int64_t (submit_graph|mem_pool_alloc_async|mem_pool_free_async)" include/shared/igpu_driver.hpp
  # 预期: 3 行匹配
  ```

- [ ] **T2.6** 验证无 `GPU_IOCTL_*` 引用：
  ```bash
  grep "GPU_IOCTL" include/shared/igpu_driver.hpp
  # 预期: 无匹配
  ```

---

## 提交与推送

- [ ] **T3.1** git add 修改的文件：
  ```bash
  cd /workspace/project/UsrLinuxEmu/external/TaskRunner
  git add include/shared/igpu_driver.hpp
  ```

- [ ] **T3.2** git commit（使用约定格式）：
  ```bash
  git commit -m "feat(igpu_driver): 15-method no-op extension for Phase 3.1/3.2 (per 4-step coordination Step 1)

  - Phase 3.1: 10 graph/capture methods (stream_capture_status/begin/end, graph_create/destroy/add_kernel_node/add_memcpy_node/instantiate, submit_graph, destroy_graph_exec)
  - Phase 3.2: 5 mempool methods (mem_pool_create/destroy/alloc/alloc_async/free_async)
  - All methods: virtual + default no-op (NOT pure virtual) for backward compat
  - 3 fence_id-return methods use int64_t (per UsrLinuxEmu B-3 + F-4)

  See openspec/changes/2026-07-05-phase3-1-igpu-driver-extension/ for details.
  Cross-repo: docs/superpowers/cross-repo-prs/2026-07-05-phase3-1-stream-mempool-coordination.md §5 Step 1.

  UsrLinuxEmu OpenSpec ACCEPTED (2026-07-05). Fix-1~Fix-14 applied. 11/11 resolutions accepted.

  Tests: 76/76 PASS (no regression). cmake build clean (no new warnings)."
  ```

- [ ] **T3.3** git push 到 origin/main：
  ```bash
  git push origin main
  ```

---

## 通知 UsrLinuxEmu

- [ ] **T4.1** 发送 Step 1 完成通知给 UsrLinuxEmu owner（参见 [`cross-repo-prs/2026-07-05-phase3-1-stream-mempool-coordination.md §12.4`](../../../superpowers/cross-repo-prs/2026-07-05-phase3-1-stream-mempool-coordination.md)）
- [ ] **T4.2** 确认 UsrLinuxEmu owner 收到，**启动 Step 2**（sim + IOCTL merge，target: 2026-07-15）

---

## 验收准则（Definition of Done）

本 change 在所有以下条件满足时视为 COMPLETE：

- [ ] IGpuDriver 方法数 31 → 46（验证：`grep -c "^    virtual " include/shared/igpu_driver.hpp` = 47）
- [ ] 15 个新方法全部**非纯虚** + 默认 `return -1`（验证：`grep "= 0;" include/shared/igpu_driver.hpp` 仅含原 31 方法的纯虚）
- [ ] 3 个 fence_id-返回方法用 `int64_t`：`submit_graph` / `mem_pool_alloc_async` / `mem_pool_free_async`
- [ ] 无任何 `GPU_IOCTL_*` 引用（验证：`grep "GPU_IOCTL" include/shared/igpu_driver.hpp` 为空）
- [ ] 现有 76+ 测试全 PASS（向后兼容验证）
- [ ] `cmake --build build` 无新增 warning
- [ ] git commit + push 成功（commit hash 记录）
- [ ] UsrLinuxEmu owner 收到 Step 1 完成通知 → 启动 Step 2

---

## 回滚预案

如发现重大 regression：

```bash
# 1. revert commit（TaskRunner main）
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
git revert <commit-hash> --no-edit
git push origin main

# 2. 通知 UsrLinuxEmu Step 1 已回滚，Step 2 暂停
```

**回滚条件**:
- 现有 76+ 测试回归 ≥1 case
- CudaStub / MockGpuDriver 编译错误（虚函数 override 不兼容）
- GpuDriverClient 编译错误（虽未修改，但确保无副作用）

**回滚后处理**:
- 在本 change 目录追加 `ROLLBACK.md` 记录回滚原因
- 与 UsrLinuxEmu owner 协调 Step 2 暂停
- 修复问题后重新提 PR

---

## 时间线总览

```
T+0:00  前置条件验证（5 min）
T+0:05  编辑 igpu_driver.hpp（+15 方法）— 15 min
T+0:20  编译 + 测试验证（10 min）
T+0:30  git commit + push（5 min）
T+0:35  通知 UsrLinuxEmu owner（5 min）

总时长: ~40 min（含 buffer）
```

**完成后**: UsrLinuxEmu 启动 Step 2（target 2026-07-15）