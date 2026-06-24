/**
 * cmd_cuda.h - CUDA CLI 命令接口
 * 
 * DDS v1.1 架构定义的 CUDA 命令接口
 */

#ifndef CMD_CUDA_H
#define CMD_CUDA_H

#include <string>
#include <cstdint>

namespace async_task {
namespace cmd {

// 全局 CUDA 设备文件描述符（在 cmd_buffer_v2.cpp 中定义）
extern int g_cuda_device_fd;

/**
 * 打印 CUDA 命令帮助信息
 */
void print_cuda_help();

/**
 * cuda_alloc <size> - 分配设备内存
 */
int cmd_cuda_alloc(int argc, char* argv[]);

/**
 * cuda_memcpy <h2d|d2h> <ptr> <offset> <size> - 内存拷贝
 */
int cmd_cuda_memcpy(int argc, char* argv[]);

/**
 * cuda_launch <kernel_name> <gx> <gy> <gz> <bx> <by> <bz> - Kernel 启动
 */
int cmd_cuda_launch(int argc, char* argv[]);

/**
 * cuda_wait <fence_id> - 等待 Fence
 */
int cmd_cuda_wait(int argc, char* argv[]);

/**
 * 分发 CUDA 命令到对应的处理器
 */
int dispatch_cuda_command(const std::string& cmd, int argc, char* argv[]);

} // namespace cmd
} // namespace async_task

#endif // CMD_CUDA_H
