# Tasks: h2-5-architecture-foundation

> **依赖**: proposal ✅ / design ✅ (D6-D11 FINALIZED) / specs ✅
> **预估总工时**: 6-8 小时（含跨仓 sync + 验证）
> **前置条件**: 无（本 change 是基础）
> **后续约束**: 本 change 完成后 H-3 `plans/2026-06-19-h3-phase2-openspec-skeleton/` 才可激活

## 1. `GpuDriverClient : public IGpuDriver`（按 D6/D7/D8 对齐 4 个 BO 签名）

- [ ] **1.1** 进入 TaskRunner：`cd external/TaskRunner`
- [ ] **1.2** 在 `include/gpu_driver_client.h` 加 `class GpuDriverClient : public IGpuDriver`
- [ ] **1.3** 改写 `alloc_bo`（按 D6）：
  ```cpp
  // 旧：(uint64_t size, uint32_t domain, uint32_t flags, uint32_t* handle, uint64_t* gpu_va) → int
  // 新：(uint64_t size, uint32_t flags) → uint64_t  // domain 折入 flags
  uint64_t alloc_bo(uint64_t size, uint32_t flags) override;
  ```
- [ ] **1.4** 改写 `alloc_bo_vram`（按 D6）：`(uint64_t size, uint32_t flags) → uint64_t`（移除外参数）
- [ ] **1.5** 改写 `free_bo`（按 D7）：`(uint64_t bo_handle) → int`（u32→u64 拓宽）
- [ ] **1.6** 改写 `map_bo`（按 D8）：`(uint64_t bo_handle, uint64_t size) → void*`（返回 CPU 指针，移除 flags + 外参数）
- [ ] **1.7** 加 `submit_batch` / `submit_memcpy` / `submit_launch` / `wait_fence` / 8 个设备信息 / 3 个生命周期 / 1 个 fd 覆盖
- [ ] **1.8** 加 `set_current_va_space` / `get_current_va_space`（snake_case H-1 迁移）+ 保留 `setCurrentVASpace` / `getCurrentVASpace` 作 deprecated alias
- [ ] **1.9** 加 5 个 H-3 占位方法（`create_va_space` 等）抛 `std::runtime_error("not implemented; see H-3")`
- [ ] **1.10** 同步适配 `src/cuda_scheduler.cpp` 中所有 4 个 BO 方法调用方（按 D6/D7/D8 新签名）
- [ ] **1.11** 同步适配 `src/cmd_cuda.cpp` 中 4 个 CLI 命令（`cmd_cuda_alloc` / `cuda_memcpy` / `cuda_launch` / `cuda_wait`）
- [ ] **1.12** 验证 `init_gpu_client()` / `shutdown_gpu_client()` / `g_gpu_client` 仍在 `async_task::gpu` 命名空间（无需改）

## 2. `CudaStub` 命名空间迁移（按 D9）+ `IGpuDriver` 实现

- [ ] **2.1** 把 `include/cuda_stub.hpp` 全部内容迁移到 `namespace async_task::gpu`（类、enum、struct 全迁移）
- [ ] **2.2** 加 `class CudaStub : public IGpuDriver`
- [ ] **2.3** 覆盖 28 个 IGpuDriver 方法（mock 语义）：
  - `open()` / `close()` → 返回 0
  - `is_open()` → `return true`（mock 总是"open"）
  - `fd()` → `return -1`（mock 无 fd）
  - `get_device_info` / `get_warp_size` 等 → 返回 canned value
  - `alloc_bo(size, flags)` → `return next_bo_handle_++`
  - `free_bo(handle)` → `return 0`
  - `map_bo(handle, size)` → `return malloc(size)`（mock 用 host malloc）
  - `submit_memcpy` / `submit_launch` / `submit_batch` → 返回 `next_fence_id_++`
  - `wait_fence` → `return 0`
  - `set_current_va_space` / `get_current_va_space` → 内部 state
  - 5 个 H-3 占位 → 返回 0 / 不抛异常（mock 容错性）
- [ ] **2.4** 保留既有 `initialize()` / `shutdown()` / `mem_alloc()` / `memcpy_h2d()` / `launch_kernel()` 等 CUDA Driver API 方法（既有调用方零修改）
- [ ] **2.5** 在文件末尾加 **deprecated alias**：
  ```cpp
  namespace taskrunner {
      using CudaStub = async_task::gpu::CudaStub;
      using CudaResult = async_task::gpu::CudaResult;
      using LaunchParams = async_task::gpu::LaunchParams;
  }
  ```
- [ ] **2.6** 同步改 `src/cuda_stub.cpp`（移到 `async_task::gpu` 命名空间）
- [ ] **2.7** 验证既有 8 个 `test_cuda_scheduler` 用例编译通过（依赖 `CudaStub` 别名）

## 3. `CudaScheduler` DI 重构（按 D10）

- [ ] **3.1** 在 `include/cuda_scheduler.hpp` 改 `CudaStub* stub_` → `async_task::gpu::IGpuDriver* driver_`
- [ ] **3.2** 改构造函数签名：
  ```cpp
  explicit CudaScheduler(async_task::gpu::IGpuDriver* driver = nullptr);
  ```
- [ ] **3.3** 构造函数逻辑：
  - `driver != nullptr` → `driver_ = driver; owns_driver_ = false`
  - `driver == nullptr` → `driver_ = new CudaStub(); owns_driver_ = true`
- [ ] **3.4** 析构函数：`if (owns_driver_) delete driver_;`
- [ ] **3.5** 在 `src/cuda_scheduler.cpp` 中所有 `stub_->xxx()` 调用改为 `driver_->xxx()`（API 兼容：IGpuDriver 28 方法与 CudaStub mock 行为一致）
- [ ] **3.6** 验证既有 8 个 `test_cuda_scheduler` 用例（`new CudaScheduler()` 无参）通过

## 4. `MockGpuDriver` 测试夹具（新增）

- [ ] **4.1** 新建 `tests/mock_gpu_driver.hpp`
- [ ] **4.2** 定义 `class MockGpuDriver : public IGpuDriver`
- [ ] **4.3** 内部 `std::vector<CallRecord>` 记录每次调用（method name + args）
- [ ] **4.4** 28 个 IGpuDriver 方法覆盖：
  - 每个方法：1. record(method, args); 2. if injected_error return sentinel; 3. return canned value
- [ ] **4.5** 加 `inject_<method>_error(bool enable)` 辅助方法（28 个）
- [ ] **4.6** 加 `history()` / `clear_history()` 访问接口
- [ ] **4.7** 加 `set_canned_<method>(return_value)` 用于精确控制返回值
- [ ] **4.8** 内部 `uint64_t next_handle_{1}` 递增返回

## 5. CLI 死调用修复（按 D11）

- [ ] **5.1** 在 `src/cli_main.cpp` 加 `#include "gpu_driver_client.h"`（引入 `init_gpu_client` / `shutdown_gpu_client`）
- [ ] **5.2** 在 `main()` 函数体开始处（argv 解析后、`cmd_buffer_v2_main` 前）：
  ```cpp
  if (async_task::gpu::init_gpu_client() != 0) {
      std::cerr << "Warning: Failed to init GPU client (running in stub mode)\n";
  }
  ```
- [ ] **5.3** 在 `main()` 退出前（`cmd_buffer_v2_main` 后）：
  ```cpp
  async_task::gpu::shutdown_gpu_client();
  ```
- [ ] **5.4** 手动验证：`./build/taskrunner cuda_alloc 4096` 不再因 `g_gpu_client == nullptr` 崩溃
- [ ] **5.5** 手动验证：`./build/taskrunner --test` 仍可用（init 失败时继续）

## 6. 测试

- [ ] **6.1** 既有 8 个 `test_cuda_scheduler` 用例全部 pass（无回归）
- [ ] **6.2** 新建 `tests/test_gpu_architecture.cpp`（doctest 框架）
- [ ] **6.3** 测试 1：`GpuDriverClient implements IGpuDriver`
  - `static_assert(std::is_base_of_v<IGpuDriver, GpuDriverClient>)` 编译期验证
- [ ] **6.4** 测试 2：`CudaStub implements IGpuDriver`
  - `static_assert(std::is_base_of_v<IGpuDriver, CudaStub>)` 编译期验证
- [ ] **6.5** 测试 3：`MockGpuDriver implements IGpuDriver`
  - `static_assert(std::is_base_of_v<IGpuDriver, MockGpuDriver>)` 编译期验证
- [ ] **6.6** 测试 4：`CudaScheduler 接受 MockGpuDriver 注入`
  - `CudaScheduler s(&mock);` 编译通过 + 运行时调 `mock.alloc_bo()` 记录到 history
- [ ] **6.7** 测试 5：`CudaScheduler nullptr 时 auto-create CudaStub`
  - `CudaScheduler s;` → `driver_` 非空 + 是 CudaStub 类型
- [ ] **6.8** 测试 6：D6/D7/D8 签名对齐
  - `gpu.alloc_bo(4096, 0)` 返回 u64 handle
  - `gpu.free_bo(handle)` 接受 u64
  - `void* p = gpu.map_bo(handle, 4096);` 返回 CPU 指针
- [ ] **6.9** 测试 7：D9 命名空间迁移
  - `async_task::gpu::CudaStub` 可实例化
  - `taskrunner::CudaStub`（alias）也可用，类型相同
- [ ] **6.10** 测试 8：D10 DI 行为
  - `CudaScheduler s(&real_gpu);` 调用转发到 `real_gpu`（不调 mock）
  - `CudaScheduler s;` 调用转发到 auto-created CudaStub
- [ ] **6.11** 在 `CMakeLists.txt` 注册新测试 target：
  ```cmake
  add_executable(test_gpu_architecture tests/test_gpu_architecture.cpp ...)
  add_test(NAME test_gpu_architecture COMMAND test_gpu_architecture)
  ```

## 7. 文档同步

- [ ] **7.1** `plans/sync-plan.md` 新增 S5 段落："✅ IGpuDriver 抽象 + 2 实现 + DI + Mock + CLI 修复"
- [ ] **7.2** `AGENTS.md` "Phase 1.5 进度" section 加 S5 ✅ Architecture foundation (2026-06-XX)
- [ ] **7.3** 更新 `README.md` 同步状态表（Phase 1.5 → S5 ✅）

## 8. 跨仓同步（仿 H-1 closeout pattern）

- [ ] **8.1** TaskRunner 仓 commit + push（参考 H-1 closeout D1）：
  ```bash
  cd external/TaskRunner
  git add -A
  git commit -m "feat(igpu): IGpuDriver abstraction + 2 implementations + DI + Mock + CLI fix (H-2.5)"
  git push -u origin feat/h2-5-architecture-foundation
  ```
- [ ] **8.2** UsrLinuxEmu 仓更新子模块指针：`git add external/TaskRunner`
- [ ] **8.3** 激活 openspec change：
  ```bash
  mv /workspace/project/UsrLinuxEmu/external/TaskRunner/plans/2026-06-19-h2-5-architecture-foundation \
     /workspace/project/UsrLinuxEmu/openspec/changes/h2-5-architecture-foundation
  ```
  激活后：移除 README.md 中 ⚠️ DRAFT 标记 + 更新 `.openspec.yaml` 的 `status: DRAFT` → `status: ACTIVE`
- [ ] **8.4** 把 openspec change 纳入 git：`git add openspec/changes/h2-5-architecture-foundation/`
- [ ] **8.5** UsrLinuxEmu 仓 commit：
  ```bash
  git commit -m "feat(h2-5): H-2.5 cross-repo sync + openspec change tracking"
  ```

## 9. 验证 + 归档

- [ ] **9.1** UsrLinuxEmu build：`make -j4` 100%
- [ ] **9.2** ctest：所有 tests pass（既有 test_gpu_plugin + test_cuda_scheduler 8/8 + test_gpu_architecture N/N）
- [ ] **9.3** docs-audit：`bash tools/docs-audit.sh --strict`
- [ ] **9.4** TaskRunner 子模块独立跑通：
  ```bash
  cd external/TaskRunner && cd build && make -j4
  ./test_cuda_scheduler   # 8/8
  ./test_gpu_architecture # N/N
  ```
- [ ] **9.5** 手动跑 CLI：`./build/taskrunner cuda_alloc 4096` 不再崩溃（D11 修复）
- [ ] **9.6** 跑 openspec 归档：`openspec archive h2-5-architecture-foundation`
- [ ] **9.7** 验证归档目录：`openspec/changes/archive/2026-06-19-h2-5-architecture-foundation/`
- [ ] **9.8** `openspec list` 期望 "No active changes"
- [ ] **9.9** archive 目录纳入 git（仿 H-1 closeout D3）：
  ```bash
  git add openspec/changes/archive/2026-06-19-h2-5-architecture-foundation/
  git commit -m "chore(openspec): archive h2-5-architecture-foundation"
  git push
  ```

## 回滚预案

| 阶段 | 回滚命令 |
|------|---------|
| §1 / §2 / §3 / §4 / §5 / §6 失败 | `git reset HEAD~1 && git restore .` (TaskRunner 仓，未 push) |
| §8.1 push 后 | `git push --force-with-lease` 回退 + revert PR |
| §8.5 UsrLinuxEmu commit | `git restore --staged openspec/changes/ external/TaskRunner` |
| §9.9 archive | `rm -rf openspec/changes/archive/2026-06-19-h2-5-architecture-foundation/` |

各阶段独立可逆。