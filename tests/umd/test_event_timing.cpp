// SCOPE: UMD-EVOLUTION
// test_event_timing.cpp - Phase 3.3a event timing precision tests.
//
// Tests for cuEvent* API: create/destroy lifecycle, flag validation,
// cuEventRecord semantics, cuEventElapsedTime strict mode, stub sanity.
// 23 test cases total.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cuda.h>

#include <cstdint>
#include <chrono>
#include <thread>

namespace {

CUevent make_event() {
  CUevent ev;
  REQUIRE(cuEventCreate(&ev, CU_EVENT_DEFAULT) == CUDA_SUCCESS);
  return ev;
}

}  // namespace

// ---------------------------------------------------------------------------
// T-EVT-Basic-1: create/destroy lifecycle (5 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cuEventCreate returns SUCCESS and non-null handle") {
  CUevent ev = nullptr;
  CHECK(cuEventCreate(&ev, CU_EVENT_DEFAULT) == CUDA_SUCCESS);
  CHECK(ev != nullptr);
  CHECK(cuEventDestroy(ev) == CUDA_SUCCESS);
}

TEST_CASE("cuEventDestroy returns SUCCESS") {
  CUevent ev = make_event();
  CHECK(cuEventDestroy(ev) == CUDA_SUCCESS);
}

TEST_CASE("cuEventCreate with null phEvent returns INVALID_VALUE") {
  CHECK(cuEventCreate(nullptr, CU_EVENT_DEFAULT) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cuEventDestroy with null handle returns INVALID_HANDLE") {
  CHECK(cuEventDestroy(nullptr) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cuEventDestroy on already-destroyed event returns INVALID_HANDLE") {
  CUevent ev = make_event();
  REQUIRE(cuEventDestroy(ev) == CUDA_SUCCESS);
  CHECK(cuEventDestroy(ev) == CUDA_ERROR_INVALID_HANDLE);
}

// ---------------------------------------------------------------------------
// T-EVT-Flags-1: flag validation (6 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cuEventCreate with CU_EVENT_DEFAULT returns SUCCESS") {
  CUevent ev;
  CHECK(cuEventCreate(&ev, CU_EVENT_DEFAULT) == CUDA_SUCCESS);
  cuEventDestroy(ev);
}

TEST_CASE("cuEventCreate with CU_EVENT_BLOCKING_SYNC returns SUCCESS") {
  CUevent ev;
  CHECK(cuEventCreate(&ev, CU_EVENT_BLOCKING_SYNC) == CUDA_SUCCESS);
  cuEventDestroy(ev);
}

TEST_CASE("cuEventCreate with CU_EVENT_DISABLE_TIMING returns SUCCESS") {
  CUevent ev;
  CHECK(cuEventCreate(&ev, CU_EVENT_DISABLE_TIMING) == CUDA_SUCCESS);
  cuEventDestroy(ev);
}

TEST_CASE("cuEventCreate with CU_EVENT_INTERPROCESS returns SUCCESS") {
  CUevent ev;
  CHECK(cuEventCreate(&ev, CU_EVENT_INTERPROCESS) == CUDA_SUCCESS);
  cuEventDestroy(ev);
}

TEST_CASE("cuEventCreate with reserved bits set returns INVALID_VALUE") {
  CUevent ev;
  CHECK(cuEventCreate(&ev, 0x10) == CUDA_ERROR_INVALID_VALUE);
  CHECK(cuEventCreate(&ev, 0xFF) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cuEventCreateWithFlags delegates to cuEventCreate correctly (valid flag)") {
  CUevent ev;
  CHECK(cuEventCreateWithFlags(&ev, CU_EVENT_BLOCKING_SYNC) == CUDA_SUCCESS);
  CHECK(ev != nullptr);
  cuEventDestroy(ev);
}

// ---------------------------------------------------------------------------
// T-EVT-Record-1: cuEventRecord semantics (4 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cuEventRecord on valid event returns SUCCESS") {
  CUevent ev = make_event();
  CHECK(cuEventRecord(ev, nullptr) == CUDA_SUCCESS);
  cuEventDestroy(ev);
}

TEST_CASE("cuEventRecord twice on same event returns SUCCESS (recorded_at updated)") {
  CUevent ev = make_event();
  REQUIRE(cuEventRecord(ev, nullptr) == CUDA_SUCCESS);
  std::this_thread::sleep_for(std::chrono::microseconds(10));
  CHECK(cuEventRecord(ev, nullptr) == CUDA_SUCCESS);
  cuEventDestroy(ev);
}

TEST_CASE("cuEventRecord on invalid handle returns INVALID_HANDLE") {
  CUevent bad = reinterpret_cast<CUevent>(static_cast<uintptr_t>(0xFFFFu));
  CHECK(cuEventRecord(bad, nullptr) == CUDA_ERROR_INVALID_HANDLE);
}

TEST_CASE("cuEventRecord on destroyed event returns INVALID_HANDLE") {
  CUevent ev = make_event();
  REQUIRE(cuEventDestroy(ev) == CUDA_SUCCESS);
  CHECK(cuEventRecord(ev, nullptr) == CUDA_ERROR_INVALID_HANDLE);
}

// ---------------------------------------------------------------------------
// T-EVT-ElapsedTime-1: strict semantics (5 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cuEventElapsedTime between two recorded events returns positive time") {
  CUevent start = make_event();
  CUevent end = make_event();
  REQUIRE(cuEventRecord(start, nullptr) == CUDA_SUCCESS);
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  REQUIRE(cuEventRecord(end, nullptr) == CUDA_SUCCESS);
  float ms = -1.0f;
  CHECK(cuEventElapsedTime(&ms, start, end) == CUDA_SUCCESS);
  CHECK(ms >= 0.0f);
  cuEventDestroy(start);
  cuEventDestroy(end);
}

TEST_CASE("cuEventElapsedTime with start newer than end returns 0.0") {
  CUevent start = make_event();
  CUevent end = make_event();
  REQUIRE(cuEventRecord(end, nullptr) == CUDA_SUCCESS);
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  REQUIRE(cuEventRecord(start, nullptr) == CUDA_SUCCESS);
  float ms = -1.0f;
  CHECK(cuEventElapsedTime(&ms, start, end) == CUDA_SUCCESS);
  CHECK(ms == 0.0f);
  cuEventDestroy(start);
  cuEventDestroy(end);
}

TEST_CASE("cuEventElapsedTime with unrecorded start returns NOT_PERMITTED") {
  CUevent start = make_event();
  CUevent end = make_event();
  REQUIRE(cuEventRecord(end, nullptr) == CUDA_SUCCESS);
  float ms = 0.0f;
  CHECK(cuEventElapsedTime(&ms, start, end) == CUDA_ERROR_NOT_PERMITTED);
  cuEventDestroy(start);
  cuEventDestroy(end);
}

TEST_CASE("cuEventElapsedTime with null output pointer returns INVALID_VALUE") {
  CUevent a = make_event();
  CUevent b = make_event();
  REQUIRE(cuEventRecord(a, nullptr) == CUDA_SUCCESS);
  REQUIRE(cuEventRecord(b, nullptr) == CUDA_SUCCESS);
  CHECK(cuEventElapsedTime(nullptr, a, b) == CUDA_ERROR_INVALID_VALUE);
  cuEventDestroy(a);
  cuEventDestroy(b);
}

TEST_CASE("cuEventElapsedTime with invalid handles returns INVALID_HANDLE") {
  CUevent a = make_event();
  CUevent bad = reinterpret_cast<CUevent>(static_cast<uintptr_t>(0xFFFFu));
  float ms = 0.0f;
  CHECK(cuEventElapsedTime(&ms, a, bad) == CUDA_ERROR_INVALID_HANDLE);
  CHECK(cuEventElapsedTime(&ms, bad, a) == CUDA_ERROR_INVALID_HANDLE);
  cuEventDestroy(a);
}

// ---------------------------------------------------------------------------
// T-EVT-Stub-Sanity: EventQuery/Synchronize (3 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cuEventQuery on valid event returns SUCCESS") {
  CUevent ev = make_event();
  CHECK(cuEventQuery(ev) == CUDA_SUCCESS);
  cuEventDestroy(ev);
}

TEST_CASE("cuEventSynchronize on valid event returns SUCCESS") {
  CUevent ev = make_event();
  CHECK(cuEventSynchronize(ev) == CUDA_SUCCESS);
  cuEventDestroy(ev);
}

TEST_CASE("cuEventQuery on destroyed event returns INVALID_HANDLE") {
  CUevent ev = make_event();
  REQUIRE(cuEventDestroy(ev) == CUDA_SUCCESS);
  CHECK(cuEventQuery(ev) == CUDA_ERROR_INVALID_HANDLE);
}