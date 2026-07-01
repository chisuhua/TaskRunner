// SCOPE: TEST-FIXTURE
/**
 * cmd_cuda.cpp - CUDA CLI 命令实现 (System C)
 *
 * 实现 6 个 CUDA 命令，使用 GPU_IOCTL_* 接口:
 * 1. cuda_alloc <size> - 分配设备内存
 * 2. cuda_memcpy <h2d|d2h> <ptr> <offset> <size> - 内存拷贝
 * 3. cuda_launch <kernel_name> <grid_x> <grid_y> <grid_z> <block_x> <block_y> <block_z> - Kernel 启动
 * 4. cuda_wait <fence_id> - 等待 Fence
 * 5. cuda_va_space create/destroy - VA Space 生命周期 (H-3 Phase 2)
 * 6. cuda_queue create/destroy - Queue 生命周期 (H-3 Phase 2)
 *
 * H-2.5 (D6/D7/D8): BO 方法签名变更
 *  - alloc_bo_vram(size, flags) -> u64 (替代旧 alloc_bo_vram(size, &handle, &gpu_va))
 *  - map_bo() 不需要 (本 CLI 不映射 BO, 仅分配 handle)
 *
 * H-3 (Phase 2): 新增 cuda_va_space + cuda_queue subcommand
 *   - cuda_va_space create/destroy: VA Space 生命周期
 *   - cuda_queue create/destroy: Queue 生命周期
 */

#include <iostream>
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "test_fixture/gpu_driver_client.h"
#include "test_fixture/cuda_scheduler.hpp"
#include "test_fixture/cuda_stub.hpp"
#include "umd/cuda_runtime_api.hpp"

namespace async_task {
namespace cmd {

// 全局 CUDA Runtime API 实例及其依赖 (B.5: UMD-EVOLUTION CLI 命令)
// 使用独立 CudaStub + CudaScheduler (不依赖 TaskRunner 单例)
static std::unique_ptr<taskrunner::CudaStub> g_runtime_stub;
static std::unique_ptr<taskrunner::CudaScheduler> g_runtime_sched;
static std::unique_ptr<async_task::umd::CudaRuntimeApi> g_runtime_api;

void print_cuda_help() {
    std::cout << "CUDA Commands (System C / GPU_IOCTL_*):\n";
    std::cout << "  cuda_alloc <size>                              - Allocate device memory\n";
    std::cout << "  cuda_memcpy <h2d|d2h> <ptr> <offset> <size>    - Memory copy (host<->device)\n";
    std::cout << "  cuda_launch <kernel> <gx> <gy> <gz> <bx> <by> <bz> - Launch kernel\n";
    std::cout << "  cuda_wait <fence_id>                           - Wait for fence\n";
    std::cout << "  cuda_va_space create <flags>                - Create VA Space, print handle\n";
    std::cout << "  cuda_va_space destroy <handle>              - Destroy VA Space by handle\n";
    std::cout << "  cuda_queue create <va_space> <type> <prio> <ring> - Create Queue, print handle\n";
    std::cout << "  cuda_queue destroy <handle>                 - Destroy Queue by handle\n";
    std::cout << "\n";
    std::cout << "CUDA Runtime API Commands (UMD-EVOLUTION Phase 1):\n";
    std::cout << "  cuda_runtime_register <name> <index>             - Register kernel name->index\n";
    std::cout << "  cuda_runtime_alloc <size>                        - Allocate device memory\n";
    std::cout << "  cuda_runtime_memcpy <h2d|d2h> <host> <dev> <sz>  - Memory copy\n";
    std::cout << "  cuda_runtime_launch <kernel_name>                - Launch registered kernel\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  cuda_alloc 4096\n";
    std::cout << "  cuda_memcpy h2d 0x1000 0 1024\n";
    std::cout << "  cuda_memcpy d2h 0x2000 0 512\n";
    std::cout << "  cuda_launch vector_add 1 1 1 256 1 1\n";
    std::cout << "  cuda_wait 1\n";
    std::cout << "  cuda_va_space create 0\n";
    std::cout << "  cuda_va_space destroy 1\n";
    std::cout << "  cuda_queue create 1 0 50 4096\n";
    std::cout << "  cuda_queue destroy 1\n";
    std::cout << "  cuda_runtime_register vectorAdd 0\n";
    std::cout << "  cuda_runtime_alloc 4096\n";
    std::cout << "  cuda_runtime_launch vectorAdd\n";
}

uint64_t parse_number(const std::string& str) {
    if (str.substr(0, 2) == "0x" || str.substr(0, 2) == "0X") {
        return std::stoull(str, nullptr, 16);
    }
    return std::stoull(str, nullptr, 10);
}

int cmd_cuda_alloc(int argc, char* argv[]) {
    if (argc < 1) {
        std::cerr << "Error: cuda_alloc requires <size> argument\n";
        return 1;
    }

    uint64_t size = parse_number(argv[0]);

    if (!::async_task::gpu::g_gpu_client || !::async_task::gpu::g_gpu_client->is_open()) {
        std::cout << "[STUB MODE] Simulating cuda_alloc\n";
        std::cout << "Allocated " << size << " bytes\n";
        std::cout << "  bo_handle: 1 (simulated)\n";
        std::cout << "  fence_id: 1 (simulated)\n";
        return 0;
    }

    // D6: alloc_bo_vram(size, flags) -> u64 bo_handle
    uint64_t bo_handle = ::async_task::gpu::g_gpu_client->alloc_bo_vram(size, 0);
    if (bo_handle == 0) {
        std::cerr << "Error: GPU_IOCTL_ALLOC_BO failed\n";
        return 1;
    }

    std::cout << "Allocated " << size << " bytes\n";
    std::cout << "  bo_handle: " << bo_handle << "\n";
    std::cout << "  (use map_bo via IGpuDriver to get CPU pointer)\n";

    return 0;
}

int cmd_cuda_memcpy(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Error: cuda_memcpy requires <h2d|d2h> <ptr> <offset> <size>\n";
        return 1;
    }

    std::string direction = argv[0];
    uint64_t ptr = parse_number(argv[1]);
    uint64_t offset = parse_number(argv[2]);
    uint64_t size = parse_number(argv[3]);

    if (!::async_task::gpu::g_gpu_client || !::async_task::gpu::g_gpu_client->is_open()) {
        std::cout << "[STUB MODE] Simulating cuda_memcpy\n";
        std::cout << "Copied " << size << " bytes (" << direction << ")\n";
        std::cout << "  device_ptr: 0x" << std::hex << ptr << std::dec << "\n";
        std::cout << "  offset: " << offset << "\n";
        std::cout << "  fence_id: 2 (simulated)\n";
        return 0;
    }

    void* host_buffer = malloc(size);
    if (!host_buffer) {
        std::cerr << "Error: Failed to allocate host buffer\n";
        return 1;
    }
    memset(host_buffer, 0, size);

    uint64_t src_addr, dst_addr;
    bool is_h2d;

    if (direction == "h2d") {
        src_addr = reinterpret_cast<uint64_t>(host_buffer);
        dst_addr = ptr + offset;
        is_h2d = true;
    } else if (direction == "d2h") {
        src_addr = ptr + offset;
        dst_addr = reinterpret_cast<uint64_t>(host_buffer);
        is_h2d = false;
    } else {
        free(host_buffer);
        std::cerr << "Error: Invalid direction '" << direction << "', use 'h2d' or 'd2h'\n";
        return 1;
    }

    if (::async_task::gpu::g_gpu_client->submit_memcpy(0, src_addr, dst_addr, size, is_h2d) < 0) {
        free(host_buffer);
        std::cerr << "Error: GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH failed\n";
        return 1;
    }

    std::cout << "Copied " << size << " bytes (" << direction << ")\n";
    std::cout << "  src_addr: 0x" << std::hex << src_addr << std::dec << "\n";
    std::cout << "  dst_addr: 0x" << std::hex << dst_addr << std::dec << "\n";

    if (!is_h2d) {
        std::cout << "  data[0..15]: ";
        uint8_t* data = static_cast<uint8_t*>(host_buffer);
        for (int i = 0; i < 16 && i < static_cast<int>(size); i++) {
            std::printf("%02x ", data[i]);
        }
        std::cout << "\n";
    }

    free(host_buffer);
    return 0;
}

int cmd_cuda_launch(int argc, char* argv[]) {
    if (argc < 7) {
        std::cerr << "Error: cuda_launch requires <kernel_name> <gx> <gy> <gz> <bx> <by> <bz>\n";
        return 1;
    }

    const char* kernel_name = argv[0];
    uint32_t grid_x = static_cast<uint32_t>(parse_number(argv[1]));
    uint32_t grid_y = static_cast<uint32_t>(parse_number(argv[2]));
    uint32_t grid_z = static_cast<uint32_t>(parse_number(argv[3]));
    uint32_t block_x = static_cast<uint32_t>(parse_number(argv[4]));
    uint32_t block_y = static_cast<uint32_t>(parse_number(argv[5]));
    uint32_t block_z = static_cast<uint32_t>(parse_number(argv[6]));

    if (!::async_task::gpu::g_gpu_client || !::async_task::gpu::g_gpu_client->is_open()) {
        std::cout << "[STUB MODE] Simulating cuda_launch\n";
        std::cout << "Launched kernel '" << kernel_name << "'\n";
        std::cout << "  grid: " << grid_x << "x" << grid_y << "x" << grid_z << "\n";
        std::cout << "  block: " << block_x << "x" << block_y << "x" << block_z << "\n";
        std::cout << "  task_id: 1 (simulated)\n";
        std::cout << "  fence_id: 3 (simulated)\n";
        return 0;
    }

    uint32_t kernel_index = 0;
    if (::async_task::gpu::g_gpu_client->submit_launch(0, kernel_index,
                                    grid_x, grid_y, grid_z,
                                    block_x, block_y, block_z) < 0) {
        std::cerr << "Error: GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH failed\n";
        std::cerr << "  kernel: " << kernel_name << "\n";
        std::cerr << "  grid: " << grid_x << "x" << grid_y << "x" << grid_z << "\n";
        std::cerr << "  block: " << block_x << "x" << block_y << "x" << block_z << "\n";
        return 1;
    }

    std::cout << "Launched kernel '" << kernel_name << "'\n";
    std::cout << "  grid: " << grid_x << "x" << grid_y << "x" << grid_z << "\n";
    std::cout << "  block: " << block_x << "x" << block_y << "x" << block_z << "\n";
    std::cout << "  kernel_index: " << kernel_index << "\n";

    return 0;
}

int cmd_cuda_wait(int argc, char* argv[]) {
    if (argc < 1) {
        std::cerr << "Error: cuda_wait requires <fence_id> argument\n";
        return 1;
    }

    uint64_t fence_id = parse_number(argv[0]);
    uint32_t timeout_ms = 0;

    if (argc >= 2) {
        timeout_ms = static_cast<uint32_t>(parse_number(argv[1]));
    }

    if (!::async_task::gpu::g_gpu_client || !::async_task::gpu::g_gpu_client->is_open()) {
        std::cout << "[STUB MODE] Simulating cuda_wait\n";
        std::cout << "Waiting for fence " << fence_id;
        if (timeout_ms > 0) {
            std::cout << " (timeout: " << timeout_ms << "ms)";
        }
        std::cout << "...\n";
        std::cout << "Fence " << fence_id << " signaled (simulated)\n";
        return 0;
    }

    std::cout << "Waiting for fence " << fence_id;
    if (timeout_ms > 0) {
        std::cout << " (timeout: " << timeout_ms << "ms)";
    }
    std::cout << "...\n";

    uint32_t status;
    if (::async_task::gpu::g_gpu_client->wait_fence(fence_id, timeout_ms, &status) < 0) {
        std::cerr << "Error: GPU_IOCTL_WAIT_FENCE failed\n";
        return 1;
    }

    std::cout << "Fence " << fence_id << " signaled (status=" << status << ")\n";

    return 0;
}

/**
 * H-3 Phase 2: cuda_va_space subcommand
 * Usage:
 *   cuda_va_space create <flags>                - Create VA Space, print va_space_handle
 *   cuda_va_space destroy <handle>              - Destroy VA Space by handle
 */
int cmd_cuda_va_space(int argc, char* argv[]) {
    if (argc < 1) {
        std::cerr << "Error: cuda_va_space requires <create|destroy> argument\n";
        return 1;
    }

    std::string subcommand = argv[0];

    if (subcommand == "create") {
        if (argc < 2) {
            std::cerr << "Error: cuda_va_space create requires <flags> argument\n";
            return 1;
        }
        uint32_t flags = static_cast<uint32_t>(parse_number(argv[1]));

        if (!::async_task::gpu::g_gpu_client || !::async_task::gpu::g_gpu_client->is_open()) {
            std::cout << "[STUB MODE] Simulating cuda_va_space create\n";
            std::cout << "va_space_handle=1 (simulated)\n";
            return 0;
        }

        uint64_t va_space_handle = ::async_task::gpu::g_gpu_client->create_va_space(flags);
        if (va_space_handle == 0) {
            std::cerr << "Error: GPU_IOCTL_CREATE_VA_SPACE failed\n";
            return 1;
        }

        std::cout << "va_space_handle=" << va_space_handle << "\n";
        return 0;

    } else if (subcommand == "destroy") {
        if (argc < 2) {
            std::cerr << "Error: cuda_va_space destroy requires <handle> argument\n";
            return 1;
        }
        uint64_t handle = parse_number(argv[1]);

        if (!::async_task::gpu::g_gpu_client || !::async_task::gpu::g_gpu_client->is_open()) {
            std::cout << "[STUB MODE] Simulating cuda_va_space destroy\n";
            std::cout << "destroyed va_space_handle=" << handle << " (simulated)\n";
            return 0;
        }

        int ret = ::async_task::gpu::g_gpu_client->destroy_va_space(handle);
        if (ret < 0) {
            std::cerr << "Error: GPU_IOCTL_DESTROY_VA_SPACE failed\n";
            return 1;
        }

        std::cout << "destroyed va_space_handle=" << handle << "\n";
        return 0;

    } else {
        std::cerr << "Error: Unknown cuda_va_space subcommand '" << subcommand << "', use 'create' or 'destroy'\n";
        return 1;
    }
}

/**
 * H-3 Phase 2: cuda_queue subcommand
 * Usage:
 *   cuda_queue create <va_space> <type> <priority> <ring_size>  - Create Queue, print queue_handle
 *   cuda_queue destroy <handle>                                  - Destroy Queue by handle
 */
int cmd_cuda_queue(int argc, char* argv[]) {
    if (argc < 1) {
        std::cerr << "Error: cuda_queue requires <create|destroy> argument\n";
        return 1;
    }

    std::string subcommand = argv[0];

    if (subcommand == "create") {
        if (argc < 5) {
            std::cerr << "Error: cuda_queue create requires <va_space> <type> <priority> <ring_size>\n";
            return 1;
        }
        uint64_t va_space_handle = parse_number(argv[1]);
        uint32_t queue_type = static_cast<uint32_t>(parse_number(argv[2]));
        uint32_t priority = static_cast<uint32_t>(parse_number(argv[3]));
        uint64_t ring_buffer_size = parse_number(argv[4]);

        if (!::async_task::gpu::g_gpu_client || !::async_task::gpu::g_gpu_client->is_open()) {
            std::cout << "[STUB MODE] Simulating cuda_queue create\n";
            std::cout << "queue_handle=1 (simulated)\n";
            return 0;
        }

        uint64_t queue_handle = ::async_task::gpu::g_gpu_client->create_queue(
            va_space_handle, queue_type, priority, ring_buffer_size);
        if (queue_handle == 0) {
            std::cerr << "Error: GPU_IOCTL_CREATE_QUEUE failed\n";
            return 1;
        }

        std::cout << "queue_handle=" << queue_handle << "\n";
        // R2 mapping: caller must save full u64 handle; submit_batch uses LOW32 as stream_id
        // (explicit & 0xFFFFFFFFULL to make truncation obvious, matches gpgpu_device.cpp:262)
        std::cout << "  (R2 mapping: stream_id=" << static_cast<uint32_t>(queue_handle & 0xFFFFFFFFULL)
                  << " = LOW32(queue_handle))\n";
        return 0;

    } else if (subcommand == "destroy") {
        if (argc < 2) {
            std::cerr << "Error: cuda_queue destroy requires <handle> argument\n";
            return 1;
        }
        uint64_t handle = parse_number(argv[1]);

        if (!::async_task::gpu::g_gpu_client || !::async_task::gpu::g_gpu_client->is_open()) {
            std::cout << "[STUB MODE] Simulating cuda_queue destroy\n";
            std::cout << "destroyed queue_handle=" << handle << " (simulated)\n";
            return 0;
        }

        int ret = ::async_task::gpu::g_gpu_client->destroy_queue(handle);
        if (ret < 0) {
            std::cerr << "Error: GPU_IOCTL_DESTROY_QUEUE failed\n";
            return 1;
        }

        std::cout << "destroyed queue_handle=" << handle << "\n";
        return 0;

    } else {
        std::cerr << "Error: Unknown cuda_queue subcommand '" << subcommand << "', use 'create' or 'destroy'\n";
        return 1;
    }
}

/**
 * B.5: 获取或创建全局 CUDA Runtime API 实例
 *
 * 使用独立 CudaStub + CudaScheduler (不依赖 TaskRunner 单例)。
 */
static bool ensure_runtime_api() {
    if (g_runtime_api) return true;

    g_runtime_stub = std::make_unique<taskrunner::CudaStub>();
    g_runtime_sched = std::make_unique<taskrunner::CudaScheduler>(g_runtime_stub.get());
    if (g_runtime_sched->initialize(true) != 0) {
        std::cerr << "Error: CudaScheduler init failed\n";
        g_runtime_sched.reset();
        g_runtime_stub.reset();
        return false;
    }
    g_runtime_api = std::make_unique<async_task::umd::CudaRuntimeApi>(g_runtime_sched.get());
    return true;
}

/**
 * B.5: cuda_runtime_register <name> <index>
 *
 * 注册 kernel 名称到索引的映射，供 cuda_runtime_launch 使用。
 */
int cmd_cuda_runtime_register(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Error: cuda_runtime_register requires <name> <index>\n";
        return 1;
    }

    std::string name = argv[0];
    std::uint32_t index = static_cast<std::uint32_t>(parse_number(argv[1]));

    if (!ensure_runtime_api()) return 1;

    auto err = g_runtime_api->register_kernel(name, index);
    if (err == async_task::umd::CudaError::Success) {
        std::cout << "[CUDA_RUNTIME] registered '" << name << "' as index " << index << std::endl;
        return 0;
    } else {
        std::cerr << "[CUDA_RUNTIME] register failed (err=" << static_cast<int>(err) << ")" << std::endl;
        return 1;
    }
}

/**
 * B.5: cuda_runtime_alloc <size>
 *
 * 使用 CudaRuntimeApi 分配设备内存。
 */
int cmd_cuda_runtime_alloc(int argc, char* argv[]) {
    if (argc < 1) {
        std::cerr << "Error: cuda_runtime_alloc requires <size>\n";
        return 1;
    }

    std::size_t size = static_cast<std::size_t>(parse_number(argv[0]));

    if (!ensure_runtime_api()) return 1;

    void* ptr = nullptr;
    auto err = g_runtime_api->malloc(&ptr, size);
    if (err == async_task::umd::CudaError::Success) {
        std::cout << "[CUDA_RUNTIME] alloc " << size << " bytes at 0x"
                  << std::hex << reinterpret_cast<std::uintptr_t>(ptr)
                  << std::dec << std::endl;
        return 0;
    } else {
        std::cerr << "[CUDA_RUNTIME] alloc failed (err=" << static_cast<int>(err) << ")" << std::endl;
        return 1;
    }
}

/**
 * B.5: cuda_runtime_memcpy <h2d|d2h> <host_ptr> <dev_ptr> <size>
 *
 * 使用 CudaRuntimeApi 在 host 和 device 之间拷贝内存。
 */
int cmd_cuda_runtime_memcpy(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Error: cuda_runtime_memcpy requires <h2d|d2h> <host_ptr> <dev_ptr> <size>\n";
        return 1;
    }

    std::string direction = argv[0];
    std::uintptr_t host_ptr = parse_number(argv[1]);
    std::uintptr_t dev_ptr = parse_number(argv[2]);
    std::size_t size = static_cast<std::size_t>(parse_number(argv[3]));

    if (!ensure_runtime_api()) return 1;

    async_task::umd::CudaMemcpyKind kind = (direction == "h2d")
        ? async_task::umd::CudaMemcpyKind::HostToDevice
        : async_task::umd::CudaMemcpyKind::DeviceToHost;

    auto err = (kind == async_task::umd::CudaMemcpyKind::HostToDevice)
        ? g_runtime_api->memcpy(reinterpret_cast<void*>(dev_ptr),
                                 reinterpret_cast<void*>(host_ptr),
                                 size, kind)
        : g_runtime_api->memcpy(reinterpret_cast<void*>(host_ptr),
                                 reinterpret_cast<void*>(dev_ptr),
                                 size, kind);

    if (err == async_task::umd::CudaError::Success) {
        std::cout << "[CUDA_RUNTIME] memcpy " << direction << " "
                  << size << " bytes OK" << std::endl;
        return 0;
    } else {
        std::cerr << "[CUDA_RUNTIME] memcpy failed (err=" << static_cast<int>(err) << ")" << std::endl;
        return 1;
    }
}

/**
 * B.5: cuda_runtime_launch <kernel_name>
 *
 * 使用 CudaRuntimeApi 启动预先注册的 kernel。
 * kernel 必须先通过 cuda_runtime_register 注册。
 */
int cmd_cuda_runtime_launch(int argc, char* argv[]) {
    if (argc < 1) {
        std::cerr << "Error: cuda_runtime_launch requires <kernel_name>\n";
        return 1;
    }

    std::string kernel_name = argv[0];

    if (!ensure_runtime_api()) return 1;

    async_task::umd::Dim3 grid{1};
    async_task::umd::Dim3 block{256};
    auto err = g_runtime_api->launch_kernel(kernel_name, grid, block, nullptr, 0);
    if (err == async_task::umd::CudaError::Success) {
        std::cout << "[CUDA_RUNTIME] launch " << kernel_name << " OK" << std::endl;
        return 0;
    } else if (err == async_task::umd::CudaError::InvalidValue) {
        std::cerr << "[CUDA_RUNTIME] kernel '" << kernel_name
                  << "' not registered; use cuda_runtime_register first" << std::endl;
        return 2;
    } else {
        std::cerr << "[CUDA_RUNTIME] launch failed (err=" << static_cast<int>(err) << ")" << std::endl;
        return 1;
    }
}

int dispatch_cuda_command(const std::string& cmd, int argc, char* argv[]) {
    if (cmd == "cuda_alloc") {
        return cmd_cuda_alloc(argc, argv);
    } else if (cmd == "cuda_memcpy") {
        return cmd_cuda_memcpy(argc, argv);
    } else if (cmd == "cuda_launch") {
        return cmd_cuda_launch(argc, argv);
    } else if (cmd == "cuda_wait") {
        return cmd_cuda_wait(argc, argv);
    } else if (cmd == "cuda_va_space") {
        return cmd_cuda_va_space(argc, argv);
    } else if (cmd == "cuda_queue") {
        return cmd_cuda_queue(argc, argv);
    } else if (cmd == "cuda_runtime_register") {
        return cmd_cuda_runtime_register(argc, argv);
    } else if (cmd == "cuda_runtime_alloc") {
        return cmd_cuda_runtime_alloc(argc, argv);
    } else if (cmd == "cuda_runtime_memcpy") {
        return cmd_cuda_runtime_memcpy(argc, argv);
    } else if (cmd == "cuda_runtime_launch") {
        return cmd_cuda_runtime_launch(argc, argv);
    } else if (cmd == "cuda_help" || cmd == "--help") {
        print_cuda_help();
        return 0;
    } else {
        std::cerr << "Error: Unknown CUDA command '" << cmd << "'\n";
        print_cuda_help();
        return 1;
    }
}

}  // namespace cmd
}  // namespace async_task
