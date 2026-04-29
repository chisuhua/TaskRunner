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
 */

#include <string>
#include <cstring>

// 声明外部 main 函数
extern int cmd_buffer_v2_main(int argc, char* argv[]);

// 前向声明测试 main 函数（仅在 BUILD_CLI=OFF 时链接）
#ifdef INCLUDE_TESTS
extern int test_main(int argc, char* argv[]);
#endif

int main(int argc, char* argv[]) {
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
        return test_main(argc, argv);
    }
#endif

    // 运行 CLI 模式（默认）
    return cmd_buffer_v2_main(argc, argv);
}
