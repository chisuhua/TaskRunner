---
SCOPE: [umd-evolution, test-fixture]
STATUS: ACTIVE
---

# Tasks: test-cu-graph-coverage-fixes (Verification Checklist)

> **状态**: 📋 **VERIFICATION MODE** — 代码已实施，当前任务是验证 + 归档
> **原目标**: Close 3 critical test gaps（`cuStreamSynchronize` no-op 修复、F-4 sim fence range 断言、`sync-plan.md` 漂移修复）
> **范围**: TaskRunner `umd-evolution` + `test-fixture` scopes。**NOT `shared`**。
> **测试基线**: `test_cu_graph` 从 30 → **32** PASS（增强 2 个既有 + 新增 2 个 TEST_CASE）
> **原 TDD 8 步**: 全部完成（commit 历史见 §A）

---

## A. 代码实施追踪（原 TDD 8 步 → 实际文件位置）

| 原 TDD Step | 内容 | 实际文件位置 | 实施状态 |
|-------------|------|-------------|----------|
| 1 | mock 加 4 个 getter + F-4 断言 | `tests/umd/test_cu_graph.cpp`（既有测试增强）| ✅ 已实施 |
| 2 | mock 实现 getter + `submit_graph`/`wait_fence` 保存返回值 | `tests/test_fixture/mock_gpu_driver.hpp:296-306`（submit_graph）、`:226-232`（wait_fence）、`:374-384`（getters）、`:437-440`（private state）| ✅ 已实施 |
| 3 | 新增 TEST_CASE #31 "Launch + cuStreamSynchronize polls fence" | `tests/umd/test_cu_graph.cpp`（grep 32 个 TEST_CASE）| ✅ 已实施 |
| 4 | 新建 `stream_fence_registry.hpp` + 实现 | `src/umd/libcuda_shim/stream_fence_registry.hpp`（全 24 行）| ✅ 已实施 |
| 5 | `cu_graph.cpp` 插入 `record_stream_fence(...)` 调用 | `src/umd/libcuda_shim/cu_graph.cpp:152` | ✅ 已实施 |
| 6 | `cuStreamSynchronize` 实现修复 | `src/umd/libcuda_shim/cu_stream.cpp:50-63` | ✅ 已实施 |
| 7 | 新增 TEST_CASE #32 "cuStreamSynchronize error propagation" | `tests/umd/test_cu_graph.cpp` | ✅ 已实施 |
| 8 | 文档同步（.openspec.yaml + sync-plan.md + SCOPE/STATUS YAML）| `openspec/changes/test-cu-graph-coverage-fixes/` 全套 | ✅ 已实施（本轮整理）|

---

## B. 验证清单（每个 checkbox 都必须 agent 可执行）

### B.1 编译验证

- [ ] **B.1.1** 编译 UMD-evolution build mode 通过：
  ```bash
  cmake -B build -DTASKRUNNER_BUILD_MODE=umd-evolution
  cmake --build build -j4
  ```
  **预期**: exit 0；无 warning 关于 `igpu_driver.hpp` 变化（因为我们没改 shared scope）。

### B.2 测试验证

- [ ] **B.2.1** `test_cu_graph` 单独运行：
  ```bash
  ctest --test-dir build -R test_cu_graph --output-on-failure
  ```
  **预期**: **32/32 PASS**（包括新增的 "Launch + cuStreamSynchronize polls fence" + "cuStreamSynchronize error propagation"）

- [ ] **B.2.2** 全量回归（确保 mock getter 改动无破坏）：
  ```bash
  ctest --test-dir build --output-on-failure
  ```
  **预期**: 所有既有测试 PASS；`test_cu_graph` 从 30 → 32 净增；其他 test binary 无 regression。

### B.3 文件存在性 + 内容校验

- [ ] **B.3.1** 新文件存在：
  ```bash
  test -f src/umd/libcuda_shim/stream_fence_registry.hpp && echo OK
  test -f openspec/changes/test-cu-graph-coverage-fixes/specs/cu-graph-async-fence-testing/spec.md && echo OK
  ```
  **预期**: 两个文件都 OK。

- [ ] **B.3.2** `cuStreamSynchronize` 不再是 no-op：
  ```bash
  grep -n "wait_fence" src/umd/libcuda_shim/cu_stream.cpp
  ```
  **预期**: 至少 1 行匹配（在 `cuStreamSynchronize` 函数体内）。

- [ ] **B.3.3** `record_stream_fence` 在 `cu_graph.cpp` 中被调用：
  ```bash
  grep -n "record_stream_fence" src/umd/libcuda_shim/cu_graph.cpp
  ```
  **预期**: 1 行匹配（line 152 附近）。

- [ ] **B.3.4** MockGpuDriver 暴露所有 6 个 getter：
  ```bash
  grep -nE "get_last_submit_graph_fence|get_last_submit_graph_exec|get_last_submit_graph_stream|get_last_wait_fence_id|get_wait_fence_call_count|reset_fence_tracking" tests/test_fixture/mock_gpu_driver.hpp
  ```
  **预期**: 至少 6 行匹配（public getters）+ 1 行匹配（reset_fence_tracking body）。

- [ ] **B.3.5** `test_cu_graph.cpp` 包含 32 个 TEST_CASE：
  ```bash
  grep -c "^TEST_CASE" tests/umd/test_cu_graph.cpp
  ```
  **预期**: 输出 `32`。

- [ ] **B.3.6** `include/shared/igpu_driver.hpp` **未修改**：
  ```bash
  git diff --name-only HEAD -- include/shared/igpu_driver.hpp
  ```
  **预期**: 空输出。

### B.4 OpenSpec 元数据一致性

- [ ] **B.4.1** 所有 SCOPE 字段统一为 `[umd-evolution, test-fixture]`：
  ```bash
  grep -l "SCOPE:" openspec/changes/test-cu-graph-coverage-fixes/*.md openspec/changes/test-cu-graph-coverage-fixes/specs/**/*.md
  ```
  预期匹配行均为：`SCOPE: [umd-evolution, test-fixture]`

- [ ] **B.4.2** openspec validate 通过：
  ```bash
  openspec validate test-cu-graph-coverage-fixes
  ```
  **预期**: `Change 'test-cu-graph-coverage-fixes' is valid`（exit 0，0 错误）

- [ ] **B.4.3** tasks.md 计数显示所有项完成：
  ```bash
  grep -c "^\- \[x\]" openspec/changes/test-cu-graph-coverage-fixes/tasks.md   # 完成的 checkbox 数
  grep -c "^\- \[ \]" openspec/changes/test-cu-graph-coverage-fixes/tasks.md   # 剩余 checkbox 数
  ```
  **预期**: 完成数 = 总数 - 5（5 个 B.1-B.4 步骤需要在本轮执行后勾选）

### B.5 文档漂移修复

- [ ] **B.5.1** `sync-plan.md` 不再引用 `test_cu_graph_real`：
  ```bash
  grep -n "test_cu_graph_real" plans/sync-plan.md
  ```
  **预期**: 空输出（或只有注释说明"已删除"）。

- [ ] **B.5.2** `sync-plan.md` 中 `test_cu_graph` 行已更新为 32：
  ```bash
  grep -n "test_cu_graph" plans/sync-plan.md
  ```
  **预期**: 至少 1 行包含 `30/30 (本 change 后 32/32)`。

- [ ] **B.5.3** `sync-plan.md` Total 行 = 245：
  ```bash
  grep -n "245/245\|270/270" plans/sync-plan.md
  ```
  **预期**: 仅 `245/245`，无 `270/270`。

### B.6 归档

- [ ] **B.6.1** 所有验证项通过后，执行归档：
  ```bash
  # 由于 openspec archive 工具对不带日期前缀的 change 名处理可能有 bug（见 openspec/TOOLING_ISSUES.md），
  # 如失败回退到手动 mv 方式。
  openspec archive test-cu-graph-coverage-fixes --yes 2>&1
  # 若失败（TOOL-001 风格），回退到：
  # mkdir -p openspec/changes/archive
  # mv openspec/changes/test-cu-graph-coverage-fixes openspec/changes/archive/
  ```
  **预期**: change 目录移入 `openspec/changes/archive/`。

- [ ] **B.6.2** 归档后验证：
  ```bash
  test -d openspec/changes/archive/test-cu-graph-coverage-fixes && echo "ARCHIVED OK"
  ls openspec/changes/archive/test-cu-graph-coverage-fixes/specs/cu-graph-async-fence-testing/
  ```
  **预期**: 归档目录存在，`spec.md` 仍可访问。

- [ ] **B.6.3** git status 检查归档重命名：
  ```bash
  git add -A
  git diff --staged -M --diff-filter=R --name-status | grep test-cu-graph-coverage-fixes
  ```
  **预期**: 显示 R100（100% rename）或类似重命名记录。

---

## C. 跨仓协调（归档后）

- [ ] **C.1** TaskRunner 仓 commit + push：
  ```bash
  git add openspec/changes/test-cu-graph-coverage-fixes/ openspec/changes/archive/
  git commit -m "openspec(archive): test-cu-graph-coverage-fixes (cuStreamSynchronize async fence coverage)

  - Fix cuStreamSynchronize no-op to wait_fence via existing 3-arg overload
  - Add stream-local fence registry (stream_fence_registry.hpp)
  - Add F-4 assertion fence_id >= 1<<32 in existing test_cu_graph case
  - Add 2 new TEST_CASE: async lifecycle + error propagation
  - Extend MockGpuDriver with submit_graph/wait_fence tracking getters
  - Fix plans/sync-plan.md drift (remove false test_cu_graph_real)
  - No shared scope change; no IGpuDriver interface change
  - test_cu_graph: 30/30 → 32/32 PASS"
  git push origin main
  ```

- [ ] **C.2** UsrLinuxEmu submodule pointer bump：
  ```bash
  cd /workspace/project/UsrLinuxEmu
  git add external/TaskRunner
  git commit -m "chore(submodule): bump TaskRunner to <sha> (test-cu-graph-coverage-fixes)"
  git push origin main
  ```

- [ ] **C.3** UsrLinuxEmu 开 tracking issue（仅通知，无合并请求）：
  ```bash
  cd /workspace/project/UsrLinuxEmu
  gh issue create \
    --title "notify: TaskRunner test-cu-graph-coverage-fixes archived (test_cu_graph 30→32 PASS)" \
    --body "TaskRunner change test-cu-graph-coverage-fixes has been archived.
    - submodule pointer bumped to <sha>
    - No UsrLinuxEmu production code changes required
    - UMD test_cu_graph now 32/32 PASS (was 30/30)
    - IGpuDriver interface unchanged
    - shared scope unchanged"
  ```

- [ ] **C.4** 检查 UsrLinuxEmu `docs/00_adr/README.md` TaskRunner TADR mirror 表是否需更新（本 change 未新增 TADR，跳过；但需确认无遗漏）：
  ```bash
  grep -A 15 "TaskRunner TADR" /workspace/project/UsrLinuxEmu/docs/00_adr/README.md | grep -i "coverage\|async fence"
  ```
  **预期**: 无新增 TADR 需 mirror（本 change 是 test-only，无新 ADR）。

---

## D. 接受准则（不可妥协）

- ✅ **B.1.1 编译 exit 0**
- ✅ **B.2.1 test_cu_graph 32/32 PASS**
- ✅ **B.2.2 全量回归无新失败**
- ✅ **B.3.6 include/shared/ 未修改**（强制）
- ✅ **B.4.2 openspec validate 0 错误**
- ✅ **B.5.x sync-plan.md 漂移全部修复**
- ✅ **未新增任何 `IGpuDriver` 虚方法**
- ✅ **未超出 test-fixture + umd-evolution 两个 scope**
- ✅ **未扩展 `cmd_cuda.cpp` 增加 `cuda_graph_*` CLI 命令**（属于未来独立 change）
- ✅ **所有 commit 信息遵循 AGENTS.md 中文/英文混用风格**

---

## E. 风险与回滚

| 风险 | 缓解 | 回滚策略 |
|------|------|----------|
| `cuStreamSynchronize` 修复破坏既有 30 个测试 | `fence_id==0` → SUCCESS 路径覆盖无 graph launch 场景 | `git revert` HEAD |
| 新增 async lifecycle 测试 flaky | mock 同步返回；`reset_fence_tracking()` 隔离 | 删除新增 TEST_CASE |
| 真实 GPU 路径未覆盖（仅 mock） | 当前 scope 是 test-fixture + umd-evolution mock；真实路径在 UsrLinuxEmu `phase4-sim-graph-launch-real-impl` 已覆盖 | 后续 Phase 4 follow-up change |
| `stream_fence_registry` 单 mutex 锁争用 | UMD shim 在测试路径单线程；生产受 GPU ioctl 限制 | Phase 5 重构为 lock-free |
| 跨仓 submodule bump 遗漏 | C.2 步骤强制执行 | UsrLinuxEmu owner 手动 bump |