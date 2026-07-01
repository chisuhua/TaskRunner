// SCOPE: TEST-FIXTURE
/**
 * cmd_buffer_v2.cpp - CLI 命令解析器 v2
 * 
 * 实现 DDS v1.1 架构的命令行接口：
 * - 解析用户输入的命令
 * - 分发到对应的命令处理器
 * - 管理 CUDA 设备文件描述符
 * 
 * 支持的命令：
 *   cuda_alloc <size>
 *   cuda_memcpy <h2d|d2h> <ptr> <offset> <size>
 *   cuda_launch <kernel_name> <gx> <gy> <gz> <bx> <by> <bz>
 *   cuda_wait <fence_id>
 */

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>

// CUDA 命令实现
#include "test_fixture/cmd_cuda.h"

namespace async_task {
namespace cmd {

// 全局 CUDA 设备文件描述符
int g_cuda_device_fd = -1;

/**
 * 打印主帮助信息
 */
void print_help(const char* program_name) {
    std::cout << "TaskRunner CLI - DDS v1.1 CUDA/Vulkan Runtime\n";
    std::cout << "\n";
    std::cout << "Usage: " << program_name << " [options] <command> [args...]\n";
    std::cout << "\n";
    std::cout << "Options:\n";
    std::cout << "  -d, --device <path>   CUDA device path (default: /dev/cuda0)\n";
    std::cout << "  -h, --help            Show this help message\n";
    std::cout << "  -v, --version         Show version information\n";
    std::cout << "\n";
    std::cout << "Commands:\n";
    std::cout << "  cuda_alloc <size>                              - Allocate device memory\n";
    std::cout << "  cuda_memcpy <h2d|d2h> <ptr> <offset> <size>    - Memory copy\n";
    std::cout << "  cuda_launch <kernel> <gx> <gy> <gz> <bx> <by> <bz> - Launch kernel\n";
    std::cout << "  cuda_wait <fence_id>                           - Wait for fence\n";
    std::cout << "  cuda_va_space create/destroy                   - VA Space lifecycle\n";
    std::cout << "  cuda_queue create/destroy                      - Queue lifecycle\n";
    std::cout << "  cuda_runtime_register <name> <index>           - Register kernel\n";
    std::cout << "  cuda_runtime_alloc <size>                      - Runtime alloc\n";
    std::cout << "  cuda_runtime_memcpy <h2d|d2h> <h> <d> <sz>    - Runtime memcpy\n";
    std::cout << "  cuda_runtime_launch <name>                     - Runtime launch\n";
    std::cout << "  cuda_help                                      - Show detailed CUDA help\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << " cuda_alloc 4096\n";
    std::cout << "  " << program_name << " -d /dev/cuda0 cuda_memcpy h2d 0x1000 0 1024\n";
    std::cout << "  " << program_name << " cuda_launch vector_add 1 1 1 256 1 1\n";
    std::cout << "  " << program_name << " cuda_wait 1\n";
    std::cout << "\n";
    std::cout << "DDS Version: 1.1.0\n";
}

/**
 * 打印版本信息
 */
void print_version() {
    std::cout << "TaskRunner CLI v1.0.0\n";
    std::cout << "DDS Architecture: v1.1.0\n";
    std::cout << "Build Date: " << __DATE__ << " " << __TIME__ << "\n";
}

/**
 * 解析命令行参数
 */
int parse_args(int argc, char* argv[], std::string& device_path, 
               std::string& command, std::vector<std::string>& args) {
    
    static struct option long_options[] = {
        {"device",  required_argument, 0, 'd'},
        {"help",    no_argument,       0, 'h'},
        {"version", no_argument,       0, 'v'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "d:hv", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'd':
                device_path = optarg;
                break;
            case 'h':
                print_help(argv[0]);
                return 0;  // 退出，不执行命令
            case 'v':
                print_version();
                return 0;  // 退出，不执行命令
            case '?':
                std::cerr << "Use '" << argv[0] << " --help' for more information\n";
                return 1;
            default:
                std::cerr << "Unknown option\n";
                return 1;
        }
    }

    // 剩余参数为命令和参数
    if (optind < argc) {
        command = argv[optind];
        for (int i = optind + 1; i < argc; i++) {
            args.push_back(argv[i]);
        }
    }

    return -1;  // 继续执行
}

/**
 * 打开 CUDA 设备
 */
bool open_cuda_device(const std::string& device_path) {
    g_cuda_device_fd = open(device_path.c_str(), O_RDWR);
    if (g_cuda_device_fd < 0) {
        std::cerr << "Warning: Could not open CUDA device '" << device_path << "'\n";
        std::cerr << "  errno=" << errno << " (" << strerror(errno) << ")\n";
        std::cerr << "  Commands will run in stub mode (no actual GPU access)\n";
        return false;
    }
    std::cout << "Opened CUDA device: " << device_path << " (fd=" << g_cuda_device_fd << ")\n";
    return true;
}

/**
 * 关闭 CUDA 设备
 */
void close_cuda_device() {
    if (g_cuda_device_fd >= 0) {
        close(g_cuda_device_fd);
        g_cuda_device_fd = -1;
    }
}

/**
 * 将 string vector 转换为 char* array（用于命令分发）
 */
std::vector<char*> to_char_array(std::vector<std::string>& args) {
    std::vector<char*> result;
    for (auto& arg : args) {
        result.push_back(&arg[0]);
    }
    return result;
}

} // namespace cmd
} // namespace async_task

/**
 * 主入口点（由 main.cpp 调用）
 */
int cmd_buffer_v2_main(int argc, char* argv[]) {
    std::string device_path = "/dev/cuda0";
    std::string command;
    std::vector<std::string> args;

    // 解析命令行参数
    int parse_result = async_task::cmd::parse_args(argc, argv, device_path, command, args);
    if (parse_result != -1) {
        return parse_result;  // 帮助或版本信息已显示
    }

    // 检查是否有命令
    if (command.empty()) {
        async_task::cmd::print_help(argv[0]);
        return 1;
    }

    // 打开 CUDA 设备（如果命令需要）
    if (command.substr(0, 5) == "cuda_") {
        async_task::cmd::open_cuda_device(device_path);
    }

    // 分发命令
    int result = 0;
    if (command.substr(0, 5) == "cuda_") {
        std::vector<char*> arg_ptrs = async_task::cmd::to_char_array(args);
        result = async_task::cmd::dispatch_cuda_command(command, 
                                                         static_cast<int>(args.size()),
                                                         arg_ptrs.data());
    } else {
        std::cerr << "Error: Unknown command prefix '" << command << "'\n";
        std::cerr << "Use '" << argv[0] << " --help' for available commands\n";
        result = 1;
    }

    // 关闭 CUDA 设备
    if (command.substr(0, 5) == "cuda_") {
        async_task::cmd::close_cuda_device();
    }

    return result;
}
