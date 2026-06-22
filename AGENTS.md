# TaskRunner - C++ Hybrid Development

## 项目架构

```
TaskRunner/
├── src/                    # 源代码
│   ├── cuda_scheduler.cpp  # CUDA 调度器 (DDS v1.2)
│   ├── cmd_cuda.cpp        # CLI CUDA 命令 (GPU_IOCTL_*)
│   ├── gpu_driver_client.cpp  # System C 封装
│   └── cuda_stub.cpp       # Stub 模式实现
├── include/                # 头文件
│   ├── gpu_driver_client.h  # GpuDriverClient 类
│   └── cuda_scheduler.hpp    # CudaScheduler 类
├── tests/
│   └── test_cuda_scheduler.cpp  # E2E 测试 (doctest)
├── UsrLinuxEmu → ../../  # 符号链接，GPU 接口定义（2026-06-19 PR #6 修复路径，原 ../UsrLinuxEmu/ 断链）
└── CMakeLists.txt
```

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
  - ⏳ H-3 (Phase 2 VA Space/Queue 真实实现，待 S5 archive 后激活)
