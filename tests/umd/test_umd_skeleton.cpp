// SCOPE: UMD-EVOLUTION
// STATUS: PROPOSED
// IMPLEMENTED: NO

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "umd/cuda_api.hpp"
#include "umd/module_loader.hpp"
#include "umd/ring_buffer.hpp"

TEST_CASE("CudaApi placeholders return nullptr/-ENOSYS") {
    async_task::umd::CudaApi api;
    CHECK(api.alloc(1024) == nullptr);
    CHECK(api.memcpy(nullptr, nullptr, 0) == -38);
    CHECK(api.launch_kernel("vec_add", nullptr, 0) == -38);
}

TEST_CASE("ModuleLoader placeholders return nullptr") {
    async_task::umd::ModuleLoader loader;
    CHECK(loader.load("kernel.hsaco") == nullptr);
    CHECK(loader.get_kernel(nullptr, "vec_add") == nullptr);
}

TEST_CASE("RingBuffer placeholders return nullptr/0/-ENOSYS") {
    async_task::umd::RingBuffer rb;
    CHECK(rb.create(64, 32) == nullptr);
    CHECK(rb.submit(nullptr, nullptr) == -38);
    CHECK_EQ(rb.ring_doorbell(nullptr, 1), 0ULL);
}
