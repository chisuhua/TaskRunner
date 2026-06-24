/**
 * cli_main.cpp - TaskRunner CLI 入口
 *
 * 提供两种模式：
 * 1. CLI 模式：执行 CUDA 命令（默认）
 * 2. 测试模式：运行原有的单元测试（通过 --test 标志）
 *
 * 编译：
 *   CLI 模式：cmake -DBUILD_CLI=ON ..
 *   测试模式：cmake -DBUILD_CLI=OFF .. (默认)
 *
 * H-2.5 (D11): 死调用修复
 *   - 启动时调用 init_gpu_client() (g_gpu_client 不再恒为 nullptr)
 *   - 退出时调用 shutdown_gpu_client()
 *   - 失败时打印警告 + 继续 (保留 --test 模式行为)
 */

#include <string>
#include <cstring>
#include <iostream>

#include "gpu_driver_client.h"

// 声明外部 main 函数
extern int cmd_buffer_v2_main(int argc, char* argv[]);

// 前向声明测试 main 函数（仅在 BUILD_CLI=OFF 时链接）
#ifdef INCLUDE_TESTS
extern int test_main(int argc, char* argv[]);
#endif

int main(int argc, char* argv[]) {
    // H-2.5 (D11): 启动时调用 init_gpu_client()
    if (async_task::gpu::init_gpu_client() != 0) {
        std::cerr << "Warning: GPU init failed, running in stub mode\n";
    }

    int ret = 0;

    // 检查是否指定了测试模式
#ifdef INCLUDE_TESTS
    bool test_mode = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--test" || std::string(argv[i]) == "-t") {
            test_mode = true;
            break;
        }
    }

    if (test_mode) {
        // 运行测试模式
        ret = test_main(argc, argv);
    } else
#endif
    {
        // 运行 CLI 模式（默认）
        ret = cmd_buffer_v2_main(argc, argv);
    }

    // H-2.5 (D11): 退出时调用 shutdown_gpu_client()
    async_task::gpu::shutdown_gpu_client();

    return ret;
}
