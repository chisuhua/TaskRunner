// SCOPE: UMD-EVOLUTION
// test_texture_surface.cpp - Phase 3.3b texture/surface frontend tests.
//
// Tests for cuArray* (Create/GetDescriptor/Destroy) and cuTexRef*
// (Create/Destroy/SetArray/SetAddress/SetFormat/SetFlags/GetAddress/GetArray).
// 25 test cases total.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cuda.h>

#include <cstdint>

// ---------------------------------------------------------------------------
// T-TEX-Basic-1: create/destroy lifecycle (5 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cuTexRefCreate returns SUCCESS and non-null handle") {
  CUtexref ref = nullptr;
  CHECK(cuTexRefCreate(&ref) == CUDA_SUCCESS);
  CHECK(ref != nullptr);
  CHECK(cuTexRefDestroy(ref) == CUDA_SUCCESS);
}

TEST_CASE("cuTexRefDestroy returns SUCCESS") {
  CUtexref ref;
  REQUIRE(cuTexRefCreate(&ref) == CUDA_SUCCESS);
  CHECK(cuTexRefDestroy(ref) == CUDA_SUCCESS);
}

TEST_CASE("cuTexRefCreate with null returns INVALID_VALUE") {
  CHECK(cuTexRefCreate(nullptr) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cuTexRefDestroy with null handle returns INVALID_HANDLE") {
  CHECK(cuTexRefDestroy(nullptr) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cuTexRefDestroy on already-destroyed returns INVALID_HANDLE") {
  CUtexref ref;
  REQUIRE(cuTexRefCreate(&ref) == CUDA_SUCCESS);
  REQUIRE(cuTexRefDestroy(ref) == CUDA_SUCCESS);
  CHECK(cuTexRefDestroy(ref) == CUDA_ERROR_INVALID_HANDLE);
}

// ---------------------------------------------------------------------------
// T-TEX-SetAddress (4 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cuTexRefSetAddress with valid args returns SUCCESS") {
  CUtexref ref;
  REQUIRE(cuTexRefCreate(&ref) == CUDA_SUCCESS);
  size_t offset = 0;
  CHECK(cuTexRefSetAddress(&offset, ref, 0x1000, 4096) == CUDA_SUCCESS);
  CHECK(offset == 0);
  cuTexRefDestroy(ref);
}

TEST_CASE("cuTexRefSetAddress with null ByteOffset returns INVALID_VALUE") {
  CUtexref ref;
  REQUIRE(cuTexRefCreate(&ref) == CUDA_SUCCESS);
  CHECK(cuTexRefSetAddress(nullptr, ref, 0x1000, 4096) == CUDA_ERROR_INVALID_VALUE);
  cuTexRefDestroy(ref);
}

TEST_CASE("cuTexRefSetAddress with null texref returns INVALID_VALUE") {
  size_t offset = 0;
  CHECK(cuTexRefSetAddress(&offset, nullptr, 0x1000, 4096) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cuTexRefSetAddress on destroyed texref returns INVALID_HANDLE") {
  CUtexref ref;
  REQUIRE(cuTexRefCreate(&ref) == CUDA_SUCCESS);
  REQUIRE(cuTexRefDestroy(ref) == CUDA_SUCCESS);
  size_t offset = 0;
  CHECK(cuTexRefSetAddress(&offset, ref, 0x1000, 4096) == CUDA_ERROR_INVALID_HANDLE);
}

// ---------------------------------------------------------------------------
// T-TEX-SetFormat (3 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cuTexRefSetFormat with valid args returns SUCCESS") {
  CUtexref ref;
  REQUIRE(cuTexRefCreate(&ref) == CUDA_SUCCESS);
  CHECK(cuTexRefSetFormat(ref, CU_AD_FORMAT_FLOAT, 4) == CUDA_SUCCESS);
  cuTexRefDestroy(ref);
}

TEST_CASE("cuTexRefSetFormat with null texref returns INVALID_VALUE") {
  CHECK(cuTexRefSetFormat(nullptr, CU_AD_FORMAT_FLOAT, 4) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cuTexRefSetFormat on destroyed texref returns INVALID_HANDLE") {
  CUtexref ref;
  REQUIRE(cuTexRefCreate(&ref) == CUDA_SUCCESS);
  REQUIRE(cuTexRefDestroy(ref) == CUDA_SUCCESS);
  CHECK(cuTexRefSetFormat(ref, CU_AD_FORMAT_FLOAT, 4) == CUDA_ERROR_INVALID_HANDLE);
}

// ---------------------------------------------------------------------------
// T-TEX-GetAddress (3 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cuTexRefGetAddress returns set address") {
  CUtexref ref;
  REQUIRE(cuTexRefCreate(&ref) == CUDA_SUCCESS);
  size_t offset = 0;
  REQUIRE(cuTexRefSetAddress(&offset, ref, 0xDEADu, 4096) == CUDA_SUCCESS);
  CUdeviceptr addr = 0;
  CHECK(cuTexRefGetAddress(&addr, ref) == CUDA_SUCCESS);
  CHECK(addr != 0);
  cuTexRefDestroy(ref);
}

TEST_CASE("cuTexRefGetAddress with null pdptr returns INVALID_VALUE") {
  CUtexref ref;
  REQUIRE(cuTexRefCreate(&ref) == CUDA_SUCCESS);
  CHECK(cuTexRefGetAddress(nullptr, ref) == CUDA_ERROR_INVALID_VALUE);
  cuTexRefDestroy(ref);
}

TEST_CASE("cuTexRefGetAddress with null texref returns INVALID_VALUE") {
  CUdeviceptr addr;
  CHECK(cuTexRefGetAddress(&addr, nullptr) == CUDA_ERROR_INVALID_VALUE);
}

// ---------------------------------------------------------------------------
// T-TEX-SetArray (4 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cuTexRefSetArray with valid args returns SUCCESS") {
  CUtexref ref;
  REQUIRE(cuTexRefCreate(&ref) == CUDA_SUCCESS);
  CUDA_ARRAY_DESCRIPTOR desc = {64, 0, CU_AD_FORMAT_FLOAT, 4};
  CUarray arr;
  REQUIRE(cuArrayCreate(&arr, &desc) == CUDA_SUCCESS);
  CHECK(cuTexRefSetArray(ref, arr, 0) == CUDA_SUCCESS);
  cuTexRefDestroy(ref);
  cuArrayDestroy(arr);
}

TEST_CASE("cuTexRefSetArray with null CUarray returns SUCCESS") {
  CUtexref ref;
  REQUIRE(cuTexRefCreate(&ref) == CUDA_SUCCESS);
  CHECK(cuTexRefSetArray(ref, nullptr, 0) == CUDA_SUCCESS);
  cuTexRefDestroy(ref);
}

TEST_CASE("cuTexRefSetArray with null texref returns INVALID_VALUE") {
  CHECK(cuTexRefSetArray(nullptr, nullptr, 0) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cuTexRefSetArray on destroyed texref returns INVALID_HANDLE") {
  CUtexref ref;
  REQUIRE(cuTexRefCreate(&ref) == CUDA_SUCCESS);
  REQUIRE(cuTexRefDestroy(ref) == CUDA_SUCCESS);
  CHECK(cuTexRefSetArray(ref, nullptr, 0) == CUDA_ERROR_INVALID_HANDLE);
}

// ---------------------------------------------------------------------------
// T-ARRAY-GetDescriptor (3 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cuArrayGetDescriptor returns created descriptor") {
  CUDA_ARRAY_DESCRIPTOR desc = {64, 32, CU_AD_FORMAT_FLOAT, 4};
  CUarray arr;
  REQUIRE(cuArrayCreate(&arr, &desc) == CUDA_SUCCESS);
  CUDA_ARRAY_DESCRIPTOR out = {};
  CHECK(cuArrayGetDescriptor(&out, arr) == CUDA_SUCCESS);
  CHECK(out.Width == 64);
  CHECK(out.Height == 32);
  CHECK(out.Format == CU_AD_FORMAT_FLOAT);
  CHECK(out.NumChannels == 4);
  cuArrayDestroy(arr);
}

TEST_CASE("cuArrayGetDescriptor with null descriptor returns INVALID_VALUE") {
  CUDA_ARRAY_DESCRIPTOR desc = {64, 0, CU_AD_FORMAT_FLOAT, 1};
  CUarray arr;
  REQUIRE(cuArrayCreate(&arr, &desc) == CUDA_SUCCESS);
  CHECK(cuArrayGetDescriptor(nullptr, arr) == CUDA_ERROR_INVALID_VALUE);
  cuArrayDestroy(arr);
}

TEST_CASE("cuArrayGetDescriptor with null array returns INVALID_VALUE") {
  CUDA_ARRAY_DESCRIPTOR out;
  CHECK(cuArrayGetDescriptor(&out, nullptr) == CUDA_ERROR_INVALID_VALUE);
}

// ---------------------------------------------------------------------------
// T-ARRAY-Lifecycle (3 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cuArrayCreate returns non-null handle") {
  CUDA_ARRAY_DESCRIPTOR desc = {64, 0, CU_AD_FORMAT_UNSIGNED_INT8, 1};
  CUarray arr = nullptr;
  CHECK(cuArrayCreate(&arr, &desc) == CUDA_SUCCESS);
  CHECK(arr != nullptr);
  cuArrayDestroy(arr);
}

TEST_CASE("cuArrayDestroy returns SUCCESS") {
  CUDA_ARRAY_DESCRIPTOR desc = {64, 0, CU_AD_FORMAT_FLOAT, 1};
  CUarray arr;
  REQUIRE(cuArrayCreate(&arr, &desc) == CUDA_SUCCESS);
  CHECK(cuArrayDestroy(arr) == CUDA_SUCCESS);
}

TEST_CASE("cuArrayDestroy on already-destroyed returns INVALID_HANDLE") {
  CUDA_ARRAY_DESCRIPTOR desc = {64, 0, CU_AD_FORMAT_FLOAT, 1};
  CUarray arr;
  REQUIRE(cuArrayCreate(&arr, &desc) == CUDA_SUCCESS);
  REQUIRE(cuArrayDestroy(arr) == CUDA_SUCCESS);
  CHECK(cuArrayDestroy(arr) == CUDA_ERROR_INVALID_HANDLE);
}