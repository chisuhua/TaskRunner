---
SCOPE: umd-evolution
STATUS: PROPOSED
---

# Tasks: test-cu-graph-coverage-fixes

> **状态**: 🚀 ACTIVE (post Oracle review, post Metis review)
> **目标**: Close 3 critical test gaps identified in `bg_40238db1` + `bg_70b42519` audits — fix `cuStreamSynchronize` no-op (BLOCKER), add F-4 sim fence range assertion, fix `sync-plan.md` drift.
> **范围**: TaskRunner `umd-evolution` + `test-fixture` scopes. **NOT `shared`**. `include/shared/igpu_driver.hpp` is NOT modified (existing `wait_fence` overloads are reused).
> **测试计数**: `test_cu_graph` 从 30 → 32（增强 2 个既有 + 新增 2 个 TEST_CASE）。

## TDD 路线（8 步）

整个 change 按 TDD 推进：每步都先有失败判据，再有实现，最后验证。

| Step | 内容 | 类型 | 失败判据 |
|------|------|------|---------|
| 1 | mock 加 4 个 getter + `test_cu_graph.cpp` 加 F-4 断言 | test | 编译失败（getter 未定义） |
| 2 | mock 实现 getter + `submit_graph`/`wait_fence` 保存返回值 | impl | #1 编译通过；F-4 断言 PASS |
| 3 | 新增 TEST_CASE #31 "Launch + cuStreamSynchronize polls fence" | test | 编译过，但 `get_wait_fence_call_count()==0`（无 no-op 已被调用） → `CHECK` 失败 |
| 4 | 新建 `stream_fence_registry.hpp` + 实现 | impl | 编译过；step 3 仍失败（cuStreamSync 仍 no-op） |
| 5 | `cu_graph.cpp` 插入 `record_stream_fence(...)` 调用 | impl | step 3 仍失败（sync 仍未查 registry） |
| 6 | `cuStreamSynchronize` 实现修复（调用 `wait_fence(fence,0,&status)`） | impl | step 3 全绿 |
| 7 | 新增 TEST_CASE #32 "cuStreamSynchronize error propagation" | test | `set_canned_return("wait_fence",1)` 后断言 `CUDA_ERROR_UNKNOWN`，初版可能返回 SUCCESS → 失败 |
| 8 | 文档同步（.openspec.yaml 字段 + sync-plan.md + SCOPE/STATUS 头）+ 全量 ctest 验证 | docs+verify | `openspec validate` 报错，或 ctest 非 0 失败 |

---

## 1. MockGpuDriver 扩展（test-fixture scope）

为 test fixture 增加非破坏性 getter + 让 mock 保存 fence 返回值。

- [ ] 1.1 在 `tests/test_fixture/mock_gpu_driver.hpp` MockGpuDriver 类私有段添加：
  ```cpp
  // ===== Phase 4 coverage: fence tracking + submit_graph param verification =====
  int64_t  last_submit_graph_fence_{-1};   // submit_graph 返回值
  uint64_t last_submit_graph_exec_{0};     // submit_graph 入参：exec_handle
  uint32_t last_submit_graph_stream_{0};   // submit_graph 入参：stream_id
  uint64_t last_wait_fence_id_{0};         // wait_fence 入参：fence_id
  ```
- [ ] 1.2 修改 `MockGpuDriver::submit_graph` override（line 295-301）：在返回前保存返回值和入参
  ```cpp
  int64_t ret = (canned != 0) ? canned
      : static_cast<int64_t>((1ull << 32) + next_fence_id_.fetch_add(1));
  last_submit_graph_fence_  = ret;
  last_submit_graph_exec_   = graph_exec_handle;  // 新增：参数验证
  last_submit_graph_stream_ = stream_id;          // 新增：参数验证
  return ret;
  ```
- [ ] 1.3 修改 `MockGpuDriver::wait_fence` 3-arg override（line 226-232）：保存传入的 `fence_id`
  ```cpp
  record("wait_fence", {fence_id, timeout_ms});
  last_wait_fence_id_ = fence_id;        // 新增：供 step 3.3 断言
  // ... 其余逻辑不变（injected_errors / status_out / canned_int）
  ```
- [ ] 1.4 在 MockGpuDriver `// Test API` 区段添加 6 个 public getter：
  ```cpp
  int64_t  get_last_submit_graph_fence()   const { return last_submit_graph_fence_; }
  uint64_t get_last_submit_graph_exec()    const { return last_submit_graph_exec_; }    // 新增
  uint32_t get_last_submit_graph_stream()  const { return last_submit_graph_stream_; }  // 新增
  uint64_t get_last_wait_fence_id()        const { return last_wait_fence_id_; }
  size_t   get_wait_fence_call_count()     const { return call_count("wait_fence"); }
  void     reset_fence_tracking() { last_submit_graph_fence_ = -1; last_submit_graph_exec_ = 0;
                                    last_submit_graph_stream_ = 0; last_wait_fence_id_ = 0; }
  ```

## 2. cu_stream.cpp + cu_graph.cpp 实现修复（umd-evolution scope）

- [ ] 2.1 **新建** `src/umd/libcuda_shim/stream_fence_registry.hpp`：
  ```cpp
  // SCOPE: UMD-EVOLUTION (internal-only header, not in include/)
  #pragma once
  #include <cstdint>
  namespace async_task::umd::shim {
  // cu_graph.cpp 在 submit_graph 成功后调用，记录该 stream 的最近 fence_id
  void record_stream_fence(void* stream, uint64_t fence_id);
  // cu_stream.cpp 的 cuStreamSynchronize 查询；0 = 无 pending fence
  uint64_t get_stream_last_fence(void* stream);
  }
  ```
- [ ] 2.2 修改 `src/umd/libcuda_shim/cu_stream.cpp`：
  - **文件顶部**（现有 `#include <cuda.h>` 等标准头之后）追加：
    ```cpp
    #include "test_fixture/gpu_driver_client.h"   // for async_task::gpu::g_gpu_client + IGpuDriver
    #include "stream_fence_registry.hpp"
    ```
    （参照 `src/umd/libcuda_shim/cu_graph.cpp:23` 已有的 include 模式。）
  - **匿名 namespace 底部**实现 registry（新增局部状态 + 暴露外部链接函数）：
    ```cpp
    namespace async_task::umd::shim {
    namespace {
    struct StreamFenceRegistry {
      std::unordered_map<CUstream, uint64_t> last_fence;
      std::mutex mu;
    };
    StreamFenceRegistry g_stream_fences;
    }
    void record_stream_fence(void* stream, uint64_t fence_id) {
      std::lock_guard<std::mutex> lock(g_stream_fences.mu);
      g_stream_fences.last_fence[stream] = fence_id;
    }
    uint64_t get_stream_last_fence(void* stream) {
      std::lock_guard<std::mutex> lock(g_stream_fences.mu);
      auto it = g_stream_fences.last_fence.find(stream);
      return (it != g_stream_fences.last_fence.end()) ? it->second : 0;
    }
    }
    ```
- [ ] 2.3 修改 `src/umd/libcuda_shim/cu_graph.cpp`：
  - **文件顶部**（`#include <cuda.h>` 之后，现有 `#include "test_fixture/gpu_driver_client.h"` 旁边）添加：
    ```cpp
    #include "stream_fence_registry.hpp"
    ```
  - 在 `cuGraphLaunch` 函数体内、submit_graph 成功并锁定 `fence_ids` 之后（约 line 144-148 处）追加：
    ```cpp
    async_task::umd::shim::record_stream_fence(hStream, static_cast<uint64_t>(fence));
    ```
- [ ] 2.4 完整替换 `src/umd/libcuda_shim/cu_stream.cpp` 的 `cuStreamSynchronize` 实现（原 line 46-49 全为 no-op）：
  ```cpp
  extern "C" CUresult cuStreamSynchronize(CUstream hStream) {
    auto* driver = async_task::gpu::g_gpu_client;
    if (!driver) return CUDA_ERROR_NOT_INITIALIZED;
    uint64_t fence_id = async_task::umd::shim::get_stream_last_fence(hStream);
    if (fence_id == 0) return CUDA_SUCCESS;          // 无 pending fence，向后兼容
    uint32_t status = 0;
    int ret = driver->wait_fence(fence_id, 0, &status);  // 0=无限等待
    if (ret != 0)    return CUDA_ERROR_UNKNOWN;
    if (status == 1) return CUDA_SUCCESS;
    if (status == 0) return CUDA_ERROR_TIMEOUT;
    if (status == static_cast<uint32_t>(-1)) return CUDA_ERROR_UNKNOWN;  // 显式 error 分支
    return CUDA_ERROR_UNKNOWN;                         // 防御式 fallback
  }
  ```

## 3. test_cu_graph.cpp 增强（umd-evolution 范围内测试）

- [ ] 3.1 增强既有测试 "Launch records fence_id to LaunchTrace"（line 330）：
  ```cpp
  // 在 cuGraphLaunch 成功之后追加：
  g_mock.reset_fence_tracking();              // 隔离
  int64_t fence = g_mock.get_last_submit_graph_fence();
  CHECK(fence >= static_cast<int64_t>(1ull << 32));   // F-4 契约
  ```
- [ ] 3.2 增强既有测试 "Launch with mock driver calls submit_graph once"（line 290）：在 cuGraphLaunch 后追加参数验证：
  ```cpp
  CHECK(g_mock.get_last_submit_graph_exec()  == static_cast<uint64_t>(exec));
  CHECK(g_mock.get_last_submit_graph_stream() == 0u);  // CU_STREAM_LEGACY
  ```
- [ ] 3.3 新增 TEST_CASE `Launch + cuStreamSynchronize polls fence`（E2E 异步生命周期）：
  ```cpp
  TEST_CASE("cu_graph: Launch + cuStreamSynchronize polls fence") {
    g_gpu_client = &g_mock;
    g_mock.clear_history();  g_mock.reset_fence_tracking();
    CUstream stream;
    REQUIRE(cuStreamCreate(&stream, 0) == CUDA_SUCCESS);
    CUgraph g;  CUgraphExec exec;
    REQUIRE(cuGraphCreate(&g, 0) == CUDA_SUCCESS);
    REQUIRE(cuGraphInstantiate(&exec, g, nullptr, nullptr, 0) == CUDA_SUCCESS);
    REQUIRE(cuGraphLaunch(exec, stream) == CUDA_SUCCESS);
    int64_t fence = g_mock.get_last_submit_graph_fence();
    REQUIRE(fence >= static_cast<int64_t>(1ull << 32));
    CHECK(cuStreamSynchronize(stream) == CUDA_SUCCESS);
    CHECK(g_mock.get_wait_fence_call_count() == 1);
    CHECK(g_mock.get_last_wait_fence_id() == static_cast<uint64_t>(fence));
    cuStreamDestroy(stream);  cuGraphExecDestroy(exec);  cuGraphDestroy(g);
    g_gpu_client = nullptr;
  }
  ```
- [ ] 3.4 新增 TEST_CASE `cuStreamSynchronize error propagation`（错误注入传播）：
  ```cpp
  TEST_CASE("cu_graph: cuStreamSynchronize error propagation") {
    g_gpu_client = &g_mock;
    g_mock.clear_history();  g_mock.reset_fence_tracking();     // 隔离模式
    g_mock.set_canned_return("wait_fence", 1);   // 非 0 = 失败
    CUstream stream;  REQUIRE(cuStreamCreate(&stream, 0) == CUDA_SUCCESS);
    CUgraph g;  CUgraphExec exec;
    REQUIRE(cuGraphCreate(&g, 0) == CUDA_SUCCESS);
    REQUIRE(cuGraphInstantiate(&exec, g, nullptr, nullptr, 0) == CUDA_SUCCESS);
    REQUIRE(cuGraphLaunch(exec, stream) == CUDA_SUCCESS);
    CHECK(cuStreamSynchronize(stream) == CUDA_ERROR_UNKNOWN);
    g_mock.set_canned_return("wait_fence", 0);   // 恢复默认
    cuStreamDestroy(stream);  cuGraphExecDestroy(exec);  cuGraphDestroy(g);
    g_gpu_client = nullptr;
  }
  ```

## 4. 文档同步

- [ ] 4.1 补全 `openspec/changes/test-cu-graph-coverage-fixes/.openspec.yaml`：
  ```yaml
  schema: spec-driven
  created: 2026-07-09
  name: test-cu-graph-coverage-fixes
  status: PROPOSED
  scope: [umd-evolution, test-fixture]
  goal: Close 3 critical cuGraph coverage gaps (cuStreamSynchronize no-op fix, F-4 fence range assertion, sync-plan documentation drift)
  baseline:
    test_cu_graph: 30/30 PASS
    test_cu_graph_real: NULL (binary does not exist; was false reference)
  ```
- [ ] 4.2 更新 `plans/sync-plan.md`（明确算术，不保留脚注）：
  - **删除** line 244 整行 `| test_cu_graph_real | ✅ 32/32 | ...`
  - **修正** line 241 `test_cu_graph | ✅ 25/25` → `| test_cu_graph | ✅ 30/30 (本次 change 后 32/32) | ...`
  - **重算 Total**：删 `-32`(test_cu_graph_real) + 加 `+7`(test_cu_graph 25→32 净增量)；新 Total = `270 − 32 + 7 = 245`
  - 将原行 `| **总计** | **270/270** | ... |` 替换为 `| **总计** | **245/245** | ... |`
  - **不保留** 270/270 脚注或"Phase 2 末期快照"等掩盖错误的注释
  - 添加本次 change 条目：`test-cu-graph-coverage-fixes (close async fence gap) — PROPOSED`
- [ ] 4.3 在 `docs/umd-evolution/roadmap/` 的 Phase 4 章节（确认 `docs/umd-evolution/roadmap/` 目录存在后定位 Phase 4 文件）添加本次 change 引用。具体定位命令：
  ```bash
  ls docs/umd-evolution/roadmap/          # 找出 Phase 4 文件
  grep -rn "test_cu_graph" docs/umd-evolution/roadmap/  # 找到引用点
  ```
  若 Phase 4 文件不存在，跳过此步骤并在 commit message 中注明"roadmap 章节待补"。

## 5. 验证 / commit / 跨仓同步

- [ ] 5.1 编译（UMD-evolution build mode）：
  ```bash
  cmake -B build -DTASKRUNNER_BUILD_MODE=umd-evolution
  cmake --build build -j4
  ```
  **预期**: exit 0，无 warning 关于 igpu_driver.hpp 的变化。
- [ ] 5.2 运行 UMD ctest：
  ```bash
  ctest --test-dir build -R test_cu_graph --output-on-failure
  ```
  **预期**: 32/32 PASS（包括新增 #31、#32 + 增强 #25、#27）。
- [ ] 5.3 运行 test-fixture 回归（确保 mock getter 改动无破坏）：
  ```bash
  ctest --test-dir build --output-on-failure
  ```
  **预期**: 所有既有测试 PASS（baseline 30 + 新增 2 = 32 in `test_cu_graph`；其他 test binary 无 regression）。
- [ ] 5.4 验证文档：
  ```bash
  openspec validate openspec/changes/test-cu-graph-coverage-fixes/
  ```
  **预期**: 0 错误。`tools/docs-audit.sh`（如存在）通过。
- [ ] 5.5 commit：
  ```bash
  git add \
    tests/test_fixture/mock_gpu_driver.hpp \
    src/umd/libcuda_shim/cu_stream.cpp \
    src/umd/libcuda_shim/cu_graph.cpp \
    src/umd/libcuda_shim/stream_fence_registry.hpp \
    tests/umd/test_cu_graph.cpp \
    plans/sync-plan.md \
    openspec/changes/test-cu-graph-coverage-fixes/
  git commit -m "test(cu_graph): close async fence coverage gaps

  - Fix cuStreamSynchronize no-op to wait_fence via existing 3-arg overload
  - Add stream-local fence registry (stream_fence_registry.hpp)
  - Add F-4 assertion fence_id >= 1<<32 in existing test_cu_graph case
  - Add 2 new TEST_CASE: async lifecycle + error propagation
  - Extend MockGpuDriver with submit_graph/wait_fence tracking getters
  - Fix plans/sync-plan.md drift (remove false test_cu_graph_real)
  - No shared scope change; no IGpuDriver interface change"
  ```
- [ ] 5.6 推送到 TaskRunner origin：
  ```bash
  git push origin main
  ```
- [ ] 5.7 跨仓通知：在 UsrLinuxEmu 仓开 issue，告知 TaskRunner change 完成（仅通知，无 PR 合并请求）：
  ```bash
  cd /workspace/project/UsrLinuxEmu
  gh issue create \
    --title "notify: TaskRunner test-cu-graph-coverage-fixes completed" \
    --body "TaskRunner change test-cu-graph-coverage-fixes has been merged.
    - submodule pointer bumped to <sha>
    - No UsrLinuxEmu production code changes required
    - UMD test_cu_graph now 32/32 PASS (was 30/30)
    - IGpuDriver interface unchanged"
  ```
  **预期**: issue 创建成功，返回 URL。
- [ ] 5.8 更新 UsrLinuxEmu 仓的 submodule 指针：
  ```bash
  cd /workspace/project/UsrLinuxEmu
  git add external/TaskRunner
  git commit -m "chore(submodule): bump TaskRunner to <sha> (test-cu-graph-coverage-fixes)"
  git push origin main
  ```

## 预期结果

| 指标 | 前 | 后 |
|------|------|------|
| `cuStreamSynchronize` 行为 | no-op（始终 SUCCESS） | 调用 `wait_fence`，按契约返回 SUCCESS/UNKNOWN/NOT_INITIALIZED |
| `test_cu_graph` 用例数 | 30 | 32 |
| `fence_id >= 1ull<<32` 断言覆盖 | ❌ | ✅（在既有 #27 "Launch records fence_id" 测试中） |
| `submit_graph` 参数验证覆盖 | ❌ | ✅（在既有 #25 "Launch with mock" 测试中） |
| 完整 async lifecycle E2E | ❌ | ✅（新增 #31） |
| `wait_fence` 错误传播 | ❌ | ✅（新增 #32） |
| `sync-plan.md` 文档漂移 | 1 处（line 244） | 0 处 |
| `shared` scope 改动 | n/a | 0 处（无 IGpuDriver 接口变更） |
| UMD-evolution ctest | 30/30 | **32/32** PASS |

## 接受准则

- 实施必须严格遵循 TDD 8 步顺序（每步先失败后实现）
- 不允许新增任何 `IGpuDriver` 虚方法
- 不允许超出 test-fixture + umd-evolution 两个 scope
- 不允许扩展 `cmd_cuda.cpp` 增加 `cuda_graph_*` CLI 命令（属于未来独立 change）
- 所有 commit 信息遵循 AGENTS.md 中描述的中文/英文混用风格
- 不允许修改 `include/shared/` 下任何文件
