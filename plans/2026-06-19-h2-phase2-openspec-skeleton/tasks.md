# Tasks: h2-phase2-implementation

> **依赖**: proposal ✅ / design ✅ (D1-D5 ❓ TBD) / specs ✅
> **预估总工时**: 4-6 小时（含 review 决策 + 跨仓 sync）
> **前置条件**: H-1 closeout (PR #6) 已合并

## 1. 决策定型：D1-D5

- [ ] **1.1** review 会议召集：TaskRunner owner + UsrLinuxEmu owner
- [ ] **1.2** D1 - VA Space 生命周期归属 决策（design §D1）
- [ ] **1.3** D2 - Queue 生命周期 决策（design §D2）
- [ ] **1.4** D3 - 方法命名风格 决策（design §D3）
- [ ] **1.5** D4 - Handle 存储 决策（design §D4）
- [ ] **1.6** D5 - 默认 VA Space 决策（design §D5）
- [ ] **1.7** 更新 `design.md` 移除 ❓ TBD 标记
- [ ] **1.8** 更新 `proposal.md §开放问题` 移除已决项
- [ ] **1.9** 更新 `specs/gpu-phase2-management/spec.md` 同步 ADDED Requirements

## 2. TaskRunner 实现

- [ ] **2.1** 进入 TaskRunner：`cd external/TaskRunner`
- [ ] **2.2** 在 `include/gpu_driver_client.h` 加 `createVASpace()` wrapper
  ```cpp
  // [TBD-D3] 命名风格根据 D3 决定
  uint64_t createVASpace(uint32_t flags = 0) {
      if (!is_open()) return 0;
      struct gpu_va_space_args args = {};
      args.flags = flags;
      if (ioctl(fd_, GPU_IOCTL_CREATE_VA_SPACE, &args) < 0) {
          std::cerr << "GpuDriverClient: GPU_IOCTL_CREATE_VA_SPACE failed"
                    << " (errno=" << errno << ")\n";
          return 0;
      }
      return args.va_space_handle;
  }
  ```
- [ ] **2.3** 加 `destroyVASpace()` wrapper
- [ ] **2.4** 加 `registerGPU()` wrapper
- [ ] **2.5** 加 `createQueue()` wrapper
- [ ] **2.6** 加 `destroyQueue()` wrapper
- [ ] **2.7** 在 `tests/test_cuda_scheduler.cpp` 加 5 个 test cases（每个 ioctl 一个）
- [ ] **2.8** 编辑 `plans/sync-plan.md` line 247-249：Phase 2 ioctls 标记 ✅ 已完成
- [ ] **2.9** 编辑 `AGENTS.md` "Phase 1.5 进度" section 加 S5 ✅ Phase 2 管理 API
- [ ] **2.10** 本地构建：`cd build && make -j4`
- [ ] **2.11** 运行测试：`./test_cuda_scheduler` — 预期 13/13 cases pass (8 现有 + 5 新)
- [ ] **2.12** TaskRunner 仓 commit + push：
  ```bash
  git add -A
  git commit -m "feat(client): Phase 2 VA Space + Queue wrappers (H-2)"
  git push -u origin feat/h2-phase2-wrappers
  ```

## 3. 跨仓同步（UsrLinuxEmu）

- [ ] **3.1** 进入 UsrLinuxEmu：`cd /workspace/project/UsrLinuxEmu`
- [ ] **3.2** 更新子模块指针：`git add external/TaskRunner`
- [ ] **3.3** 把本 openspec change 迁入正式位置：
  ```bash
  mv /workspace/project/UsrLinuxEmu/external/TaskRunner/plans/2026-06-19-h2-phase2-openspec-skeleton \
     /workspace/project/UsrLinuxEmu/openspec/changes/h2-phase2-implementation
  ```
  （激活后：移除 README.md 中 ⚠️ DRAFT 标记 + 更新 .openspec.yaml 移除 `status: DRAFT`）
- [ ] **3.4** 把 openspec change 纳入 git：`git add openspec/changes/h2-phase2-implementation/`
- [ ] **3.5** 验证文件：`git ls-tree HEAD openspec/changes/h2-phase2-implementation/`
- [ ] **3.6** UsrLinuxEmu 仓 commit：
  ```bash
  git commit -m "fix(h2-closeout): H-2 cross-repo sync + openspec change tracking"
  ```

## 4. 验证 (UsrLinuxEmu)

- [ ] **4.1** build：`make -j4` 100%
- [ ] **4.2** ctest：所有 tests pass
- [ ] **4.3** docs-audit：`bash tools/docs-audit.sh --strict`
- [ ] **4.4** TaskRunner 子模块测试独立跑通：
  ```bash
  cd external/TaskRunner && cd build && make -j4 && ./test_cuda_scheduler
  # 预期 13/13 cases
  ```

## 5. 归档

- [ ] **5.1** 跑 openspec 归档：`openspec archive h2-phase2-implementation`
- [ ] **5.2** 验证归档目录：`openspec/changes/archive/2026-06-19-h2-phase2-implementation/`
- [ ] **5.3** `openspec list` 期望 "No active changes"
- [ ] **5.4** archive 目录纳入 git：
  ```bash
  git add openspec/changes/archive/2026-06-19-h2-phase2-implementation/
  git commit -m "chore(openspec): archive h2-phase2-implementation"
  git push
  ```

## 回滚预案

| 阶段 | 回滚命令 |
|------|---------|
| §2 TaskRunner commit | `git reset HEAD~1 && git restore .` (TaskRunner 仓，未 push) |
| §2.12 TaskRunner push 后 | `git push --force-with-lease` 回退 + revert PR |
| §3.6 UsrLinuxEmu commit | `git restore --staged openspec/changes/ external/TaskRunner` |
| §5 archive | `rm -rf openspec/changes/archive/2026-06-19-h2-phase2-implementation/` |

各阶段独立可逆。