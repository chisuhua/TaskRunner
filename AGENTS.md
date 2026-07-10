# TaskRunner - C++ Hybrid Development

## 项目架构 (H-5 3-scope)

```
TaskRunner/
├── src/                              # 源代码 (按 scope 分目录)
│   ├── test_fixture/                 # test-fixture scope (default)
│   │   ├── cuda_scheduler.cpp        # CUDA 调度器 (DDS v1.2)
│   │   ├── cmd_cuda.cpp              # CLI CUDA 命令 (GPU_IOCTL_*)
│   │   ├── gpu_driver_client.cpp     # System C 封装
│   │   ├── cuda_stub.cpp             # Stub 模式实现
│   │   ├── TaskRunner.cpp            # 单例调度器
│   │   ├── CmdProcessor.cpp          # 工作线程
│   │   ├── cmd_buffer_v2.cpp         # V2 命令缓冲
│   │   └── cli_main.cpp              # CLI 入口
│   ├── umd/                          # umd-evolution scope (experimental)
│   │   ├── cuda_runtime_api.cpp              # CudaRuntimeApi (Phase 1)
│   └── shared/                       # shared scope (cross-cutting)
│       ├── memory_manager.cpp        # 共享内存管理器
│       └── sync_primitives.cpp       # 同步原语实现
├── include/                          # 头文件 (按 scope 分目录)
│   ├── test_fixture/                 # test-fixture 头文件
│   │   ├── gpu_driver_client.h       # GpuDriverClient 类
│   │   ├── cuda_scheduler.hpp        # CudaScheduler 类
│   │   ├── cuda_stub.hpp             # Stub 模式头
│   │   ├── TaskRunner.h              # 单例类
│   │   ├── CmdProcessor.h            # 工作线程类
│   │   ├── CmdStream.h / CmdBuffer.h / EventQueue.h / TaskQueue.h / TaskBuffer.h / Barrier.h
│   │   └── cmd_cuda.h                # CLI 命令声明
│   ├── umd/                          # umd-evolution 头文件
│   │   ├── cuda_runtime_api.hpp         # CudaRuntimeApi (Phase 1)
│   └── shared/                       # shared 头文件
│       ├── igpu_driver.hpp           # IGpuDriver 接口 (28→31 方法)
│       ├── sync_primitives.hpp       # 同步原语抽象
│       ├── memory_manager.hpp        # 内存管理器
│       └── error_handling.hpp        # Result<T> + ErrorCode
├── tests/                            # 测试 (按 scope 分目录)
│   ├── test_fixture/                 # test-fixture 测试 (doctest)
│   ├── umd/                          # umd-evolution 测试
│   └── shared/                       # shared 测试
├── docs/                             # 文档 (按 scope 分目录)
│   ├── test-fixture/{adr,architecture,roadmap,archive}/
│   ├── umd-evolution/{adr,architecture,roadmap,archive}/
│   └── shared/{adr,research}/
├── tools/
│   └── docs-audit.sh                 # 文档验证脚本
├── UsrLinuxEmu → ../../              # 符号链接，GPU 接口定义（2026-06-19 PR #6 修复路径）
├── .rddf/wt/                          # Git worktree 目录（项目内约定，已 gitignore）
└── CMakeLists.txt
```

## Git Worktree 约定 (2026-07-06)

**项目内约定**：所有 worktree 必须在项目根目录下 `.rddf/wt/<branch>` 创建，**不再使用** `~/.config/superpowers/worktrees/` 全局目录。

### 创建示例

```bash
# 新建 worktree（绝对路径必须落在 .rddf/wt/）
git worktree add .rddf/wt/feature-xyz -b feature-xyz main

# 列出
git worktree list

# 完成后清理
git worktree remove .rddf/wt/feature-xyz
git branch -d feature-xyz
git worktree prune
```

### 路径约束

- **允许**：`<repo-root>/.rddf/wt/<branch>/`
- **禁止**：`~/.config/superpowers/worktrees/<repo>/<branch>/`（旧路径，已废弃）
- **禁止**：项目根下其他位置（避免污染工作树）

### 历史遗留清理

2026-07-06 清理：`/home/ubuntu/.config/superpowers/worktrees/TaskRunner/phase3-1-igpu-driver-extension`（已 merge 进 main，分支已删）。

## 构建命令

```bash
# 初始化 clangd LSP (首次使用或克隆后)
./init.sh 或 ~/.config/opencode/scripts/init-clangd.sh .

# 配置 (test 模式)
mkdir -p build && cd build && cmake ..

# 构建 test 模式
cd build && make -j4

# 构建 CLI 模式
cd build && cmake .. -DBUILD_CLI=ON && make -j4

# 运行测试
./build/test_cuda_scheduler

# 运行 CLI
./build/taskrunner cuda_alloc 4096
```

## 关键约束

### GPU 接口 (System C)
- **Canonical Source**: `UsrLinuxEmu/plugins/gpu_driver/shared/gpu_ioctl.h`
- TaskRunner 通过符号链接访问 UsrLinuxEmu
- 使用 `GPU_IOCTL_*` (magic='G') 而非 `CUDA_IOCTL_*` (magic='C')
- 符号链接断裂时 CMake 报错退出

### 命名规范
- 类名: CamelCase (`GpuDriverClient`)
- 函数名: camelCase (`submitMemcpy`)
- 变量名: snake_case (`device_ptr`)
- 命名空间: `async_task::gpu`, `async_task::cmd`

### 编码风格
- 缩进: 2 空格 (禁止 Tab)
- 中文注释
- 文件头包含功能描述、作者、日期

## UsrLinuxEmu 联调

### 插件命名
- **必须**以 `plugin_` 开头 (ModuleLoader 过滤逻辑)
- 错误: `gpu_driver_plugin.so` → 正确: `plugin_gpu_driver.so`

### 运行联调测试
```bash
cd UsrLinuxEmu
./build/bin/test_gpu_plugin           # GPU 插件测试
./build/bin/test_gpu_ioctl_standalone  # 旧版 ioctl 测试
```

## 已知问题

- ~~插件 teardown 时 SIGSEGV~~ - Issue #13 ✅ **已修复** (2026-05-09, commit dd81e5c)
  - 修复: plugin_fini_internal() 销毁顺序改为 stop puller → reset → unregister device
  - 验证: 所有测试 (test_gpu_plugin 等) 均正常 teardown，无 SIGSEGV

## GitHub Issues

- TaskRunner: https://github.com/chisuhua/TaskRunner/issues/5 (Phase 1 完成)
- UsrLinuxEmu:
  - Issue #11: VFS 单例问题 (已修复)
  - Issue #12: Phase 1.5 fence_id 扩展 (S3.5 已完成, 2026-05-13)
  - Issue #13: Teardown SIGSEGV (已修复, 2026-05-09, commit dd81e5c)

## 状态追踪

- 同步点 S0-S4 已完成 ✅
- Phase 1 联调完成 (2026-04-29) ✅
- Phase 1.5 进度:
  - ✅ S3.5 fence_id 返回机制 (2026-05-13, main commit a7f4463)
  - ✅ S3.1 va_space_handle 透传 (2026-06-17, PR #6)
  - ✅ S5 Architecture foundation (2026-06-19, UsrLinuxEmu commit c64301c) — IGpuDriver 抽象 + GpuDriverClient/CudaStub 实现 + CudaScheduler DI + MockGpuDriver + CLI 死调用修复
  - ✅ H-3 (Phase 2 VA Space/Queue 真实实现，2026-06-23，commits 241f3ed..8625b82) — IGpuDriver 5 Phase 2 方法完整实现 (GpuDriverClient + CudaStub + tests/test_gpu_phase2.cpp 12 cases + CLI cuda_va_space/cuda_queue)

## 跨仓工作原则

TaskRunner 是 UsrLinuxEmu 的 git submodule (`external/TaskRunner`)。当 session 启动在本目录 (`/workspace/project/UsrLinuxEmu/external/TaskRunner`) 时，**默认启用跨仓工作模式**。

### 同步检查

每次改动后必须同步检查 UsrLinuxEmu 仓是否需要更新：

```bash
# 1. 检查 submodule 指针变更
cd /workspace/project/UsrLinuxEmu && git status -s external/TaskRunner

# 2. 检查 ADR-035 INDEX 是否需新增 TaskRunner TADR mirror
grep -A 15 "TaskRunner TADR" /workspace/project/UsrLinuxEmu/docs/00_adr/README.md

# 3. 检查 openspec/ changes 是否有跨仓关联
cd /workspace/project/UsrLinuxEmu && git diff main HEAD --stat | grep external/TaskRunner
```

### 同步协议 (ADR-035 §Rule 5.1 4 步)

1. **TaskRunner 仓** (`/workspace/project/UsrLinuxEmu/external/TaskRunner`)：
   ```bash
   git add + commit + git push origin main
   ```

2. **UsrLinuxEmu 仓** (`/workspace/project/UsrLinuxEmu`)：
   ```bash
   cd /workspace/project/UsrLinuxEmu
   git add external/TaskRunner                   # 更新 submodule 指针
   git commit -m "chore(submodule): bump TaskRunner to <hash>"
   git push origin main
   ```

3. **如新增 TADR-NNN**：
   ```bash
   cd /workspace/project/UsrLinuxEmu
   # 更新 docs/00_adr/README.md "TaskRunner TADR mirror" 段
   git add docs/00_adr/README.md
   git commit -m "docs(adr): update TaskRunner TADR mirror (TADR-NNN)"
   git push origin main
   ```

### 跨仓文档引用规范 (H-5 3-scope)

| 从 → 到 | 路径深度 | 示例 |
|--------|--------|------|
| TaskRunner `docs/{test-fixture,umd-evolution,shared}/adr/` → UsrLinuxEmu `docs/00_adr/` | `../../../../` (4 dots) | `../../../../docs/00_adr/adr-032-...md` |
| TaskRunner `docs/{test-fixture,umd-evolution}/archive/` → UsrLinuxEmu `docs/00_adr/` | `../../../../` (4 dots) | `../../../../docs/00_adr/adr-032-...md` |
| TaskRunner `docs/{test-fixture,umd-evolution,shared}/adr/` → TaskRunner `src/{test_fixture,umd,shared}/` | `../../` (2 dots) | `../../src/test_fixture/cuda_stub.cpp` |
| UsrLinuxEmu `docs/` → TaskRunner `docs/` | `../external/TaskRunner/` | `../external/TaskRunner/docs/shared/adr/README.md` |
| TaskRunner 跨 scope 引用（如 test-fixture/adr → shared/adr） | `../../shared/adr/` | `../../shared/adr/tadr-301-igpu-driver-contract.md` |

### 工作计划考虑

制定计划时需考虑跨仓更新的影响范围：

- **改动 TADR-NNN**：需更新 UsrLinuxEmu `docs/00_adr/README.md` mirror 表
- **改动 Phase 文档**：可能需更新 UsrLinuxEmu `openspec/changes/<related-change>/`
- **改动 submodule pointer**：需协调 UsrLinuxEmu owner 的 PR 流程（ADR-035 R5.1）
- **新增归档文件**：TaskRunner 端 `git mv` + UsrLinuxEmu 端无需变动
- **新增 roadmap phase**：TaskRunner 端 + UsrLinuxEmu sync-plan.md 同步

## Scope Classification (H-5)

All TaskRunner content MUST be classified into one of three scopes:

- **test-fixture** (`docs/test-fixture/`, `include/test_fixture/`, `src/test_fixture/`, `tests/test_fixture/`):
  Currently-shippable state. STATUS: ACCEPTED. Code is in main branch.
- **umd-evolution** (`docs/umd-evolution/`, `include/umd/`, `src/umd/`, `tests/umd/`):
  Experimental vision + skeleton. STATUS: PROPOSED/DRAFT only. Code compiles only under `TASKRUNNER_BUILD_MODE=umd-evolution`.
- **shared** (`docs/shared/`, `include/shared/`, `src/shared/`, `tests/shared/`):
  Cross-cutting abstractions. STATUS: ACCEPTED. Dual review required for changes.

### Required Metadata

Every document header MUST include:

```markdown
---
SCOPE: <test-fixture|umd-evolution|shared>
STATUS: <ACCEPTED|PROPOSED|DRAFT|DEPRECATED>
---
```

Every `.hpp`/`.cpp`/`.h` file MUST have `// SCOPE: <scope>` as first line.

### Cross-Scope References

When test-fixture docs reference umd-evolution content, use relative path `../umd-evolution/...` and tag inline as `[UMD-EVOLUTION SCOPE]`.

### Build Mode Selection

> **Changed 2026-07-09**: Default is now **umd-evolution** (was test-fixture).
> See `openspec/changes/umd-evolution-build-default-on/` and superseded ADR
> `docs/shared/adr/tadr-108-build-mode-selection.md`.

```bash
# Default (umd-evolution — includes libcuda_shim + tests/umd)
cmake -B build

# Opt-out: test-fixture only (excludes libcuda_shim + tests/umd;
# src/umd/cuda_runtime_api.cpp still compiled as part of CLI)
cmake -B build -DTASKRUNNER_BUILD_MODE=test-fixture

# Explicit umd-evolution (alias for default; kept for backward compat)
cmake -B build -DTASKRUNNER_BUILD_MODE=umd-evolution
```
