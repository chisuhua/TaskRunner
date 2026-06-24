/**
 * gpu_driver_client.cpp - System C 封装层实现
 */

#include "test_fixture/gpu_driver_client.h"

namespace async_task {
namespace gpu {

GpuDriverClient* g_gpu_client = nullptr;

int init_gpu_client() {
    if (g_gpu_client) {
        return 0;
    }
    g_gpu_client = new GpuDriverClient();
    if (g_gpu_client->open() != 0) {
        delete g_gpu_client;
        g_gpu_client = nullptr;
        return -1;
    }
    return 0;
}

void shutdown_gpu_client() {
    if (g_gpu_client) {
        g_gpu_client->close();
        delete g_gpu_client;
        g_gpu_client = nullptr;
    }
}

}  // namespace gpu
}  // namespace async_task
