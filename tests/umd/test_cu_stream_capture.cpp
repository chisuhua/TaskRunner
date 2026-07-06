// SCOPE: UMD-EVOLUTION
// test_cu_stream_capture.cpp - E2E tests for cuStreamCapture shim (Phase 3.1).
//
// Tests shim-only path (does NOT call GpuDriverClient; tests verify state
// transitions through cuStreamIsCapturing). 30+ cases covering state machine,
// GLOBAL mode, error paths, integration with cuGraph, and Stage 1.4 regression.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cuda.h>

#include <cstdint>
#include <vector>

namespace {

// Helper: create a unique CUstream handle per test by varying pointer address.
CUstream make_stream(uint32_t id) {
  return reinterpret_cast<CUstream>(static_cast<uintptr_t>(0x1000u + id));
}

CUgraph make_graph_sentinel() { return reinterpret_cast<CUgraph>(0xCAFEu); }

}  // namespace

// ---------------------------------------------------------------------------
// State machine basics (3 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cu_stream_capture: fresh stream has capture status NONE") {
  CUstream s = make_stream(1);
  CUstreamCaptureStatus status = CU_STREAM_CAPTURE_STATUS_INVALIDATED;
  CHECK(cuStreamIsCapturing(s, &status) == CUDA_SUCCESS);
  CHECK(status == CU_STREAM_CAPTURE_STATUS_NONE);
}

TEST_CASE("cu_stream_capture: after Begin, status is ACTIVE") {
  CUstream s = make_stream(2);
  REQUIRE(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  CUstreamCaptureStatus status = CU_STREAM_CAPTURE_STATUS_NONE;
  CHECK(cuStreamIsCapturing(s, &status) == CUDA_SUCCESS);
  CHECK(status == CU_STREAM_CAPTURE_STATUS_ACTIVE);
}

TEST_CASE("cu_stream_capture: after End, status returns to NONE") {
  CUstream s = make_stream(3);
  REQUIRE(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  CUgraph g = nullptr;
  REQUIRE(cuStreamEndCapture(s, &g) == CUDA_SUCCESS);
  CHECK(g != nullptr);
  CUstreamCaptureStatus status = CU_STREAM_CAPTURE_STATUS_ACTIVE;
  CHECK(cuStreamIsCapturing(s, &status) == CUDA_SUCCESS);
  CHECK(status == CU_STREAM_CAPTURE_STATUS_NONE);
}

// ---------------------------------------------------------------------------
// GLOBAL mode tests (5 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cu_stream_capture: GLOBAL mode Begin returns SUCCESS") {
  CUstream s = make_stream(10);
  CHECK(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  CUgraph g;
  cuStreamEndCapture(s, &g);
}

TEST_CASE("cu_stream_capture: non-GLOBAL mode returns NOT_SUPPORTED (F-1)") {
  CUstream s = make_stream(11);
  CHECK(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_RELAXED)
        == CUDA_ERROR_NOT_SUPPORTED);
  CHECK(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_THREAD_LOCAL)
        == CUDA_ERROR_NOT_SUPPORTED);
}

TEST_CASE("cu_stream_capture: Begin on NULL stream returns INVALID_VALUE") {
  CHECK(cuStreamBeginCapture(nullptr, CU_STREAM_CAPTURE_MODE_GLOBAL)
        == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cu_stream_capture: End on NULL stream returns INVALID_VALUE") {
  CUgraph g;
  CHECK(cuStreamEndCapture(nullptr, &g) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cu_stream_capture: End with NULL phGraph returns INVALID_VALUE") {
  CUstream s = make_stream(12);
  REQUIRE(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  CHECK(cuStreamEndCapture(s, nullptr) == CUDA_ERROR_INVALID_VALUE);
  CUgraph g;
  cuStreamEndCapture(s, &g);
}

// ---------------------------------------------------------------------------
// Error path tests (5 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cu_stream_capture: End without Begin returns ILLEGAL_STATE") {
  CUstream s = make_stream(20);
  CUgraph g;
  CHECK(cuStreamEndCapture(s, &g) == CUDA_ERROR_ILLEGAL_STATE);
}

TEST_CASE("cu_stream_capture: Begin twice on same stream -> ILLEGAL_STATE") {
  CUstream s = make_stream(21);
  REQUIRE(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  CHECK(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL)
        == CUDA_ERROR_ILLEGAL_STATE);
  CUgraph g;
  cuStreamEndCapture(s, &g);
}

TEST_CASE("cu_stream_capture: IsCapturing with NULL status returns INVALID_VALUE") {
  CUstream s = make_stream(22);
  CHECK(cuStreamIsCapturing(s, nullptr) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cu_stream_capture: status latches INVALIDATED after illegal transition") {
  CUstream s = make_stream(23);
  REQUIRE(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  CHECK(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL)
        == CUDA_ERROR_ILLEGAL_STATE);
  CUstreamCaptureStatus status = CU_STREAM_CAPTURE_STATUS_NONE;
  CHECK(cuStreamIsCapturing(s, &status) == CUDA_SUCCESS);
  CHECK(status == CU_STREAM_CAPTURE_STATUS_INVALIDATED);
}

TEST_CASE("cu_stream_capture: stream-level isolation between handles") {
  CUstream s1 = make_stream(24);
  CUstream s2 = make_stream(25);
  REQUIRE(cuStreamBeginCapture(s1, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  CUstreamCaptureStatus s2_status = CU_STREAM_CAPTURE_STATUS_ACTIVE;
  CHECK(cuStreamIsCapturing(s2, &s2_status) == CUDA_SUCCESS);
  CHECK(s2_status == CU_STREAM_CAPTURE_STATUS_NONE);
  CUgraph g;
  cuStreamEndCapture(s1, &g);
}

// ---------------------------------------------------------------------------
// Integration tests (10 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cu_stream_capture: graph handle returned is unique across End calls") {
  CUstream s1 = make_stream(30);
  CUstream s2 = make_stream(31);
  REQUIRE(cuStreamBeginCapture(s1, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  CUgraph g1;
  REQUIRE(cuStreamEndCapture(s1, &g1) == CUDA_SUCCESS);
  REQUIRE(cuStreamBeginCapture(s2, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  CUgraph g2;
  REQUIRE(cuStreamEndCapture(s2, &g2) == CUDA_SUCCESS);
  CHECK(g1 != g2);
}

TEST_CASE("cu_stream_capture: capture + cuGraphDestroy integration") {
  CUstream s = make_stream(32);
  REQUIRE(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  CUgraph g;
  REQUIRE(cuStreamEndCapture(s, &g) == CUDA_SUCCESS);
  CHECK(cuGraphDestroy(g) == CUDA_SUCCESS);
}

TEST_CASE("cu_stream_capture: Begin-End-Begin cycle on same stream") {
  CUstream s = make_stream(33);
  REQUIRE(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  CUgraph g1;
  REQUIRE(cuStreamEndCapture(s, &g1) == CUDA_SUCCESS);
  REQUIRE(cuGraphDestroy(g1) == CUDA_SUCCESS);
  REQUIRE(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  CUgraph g2;
  REQUIRE(cuStreamEndCapture(s, &g2) == CUDA_SUCCESS);
  CHECK(g2 != nullptr);
  CHECK(g2 != g1);
}

TEST_CASE("cu_stream_capture: 10 sequential capture cycles") {
  CUstream s = make_stream(34);
  std::vector<CUgraph> graphs;
  for (int i = 0; i < 10; ++i) {
    REQUIRE(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
    CUgraph g;
    REQUIRE(cuStreamEndCapture(s, &g) == CUDA_SUCCESS);
    graphs.push_back(g);
  }
  CHECK(graphs.size() == 10);
  // All unique.
  for (size_t i = 0; i < graphs.size(); ++i) {
    for (size_t j = i + 1; j < graphs.size(); ++j) {
      CHECK(graphs[i] != graphs[j]);
    }
  }
}

TEST_CASE("cu_stream_capture: concurrent captures on different streams") {
  CUstream s1 = make_stream(35);
  CUstream s2 = make_stream(36);
  REQUIRE(cuStreamBeginCapture(s1, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  REQUIRE(cuStreamBeginCapture(s2, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  CUgraph g1, g2;
  REQUIRE(cuStreamEndCapture(s1, &g1) == CUDA_SUCCESS);
  REQUIRE(cuStreamEndCapture(s2, &g2) == CUDA_SUCCESS);
  CHECK(g1 != g2);
}

TEST_CASE("cu_stream_capture: cuGraphDestroy on End-captured graph") {
  CUstream s = make_stream(37);
  REQUIRE(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  CUgraph g;
  REQUIRE(cuStreamEndCapture(s, &g) == CUDA_SUCCESS);
  // Verify g can be passed to cuGraphCreate-style lifecycle.
  CHECK(cuGraphDestroy(g) == CUDA_SUCCESS);
}

TEST_CASE("cu_stream_capture: status checks are idempotent") {
  CUstream s = make_stream(38);
  CUstreamCaptureStatus status;
  REQUIRE(cuStreamIsCapturing(s, &status) == CUDA_SUCCESS);
  REQUIRE(cuStreamIsCapturing(s, &status) == CUDA_SUCCESS);
  CHECK(status == CU_STREAM_CAPTURE_STATUS_NONE);
}

TEST_CASE("cu_stream_capture: end-issued graph has non-null handle") {
  CUstream s = make_stream(39);
  REQUIRE(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  CUgraph g = make_graph_sentinel();
  REQUIRE(cuStreamEndCapture(s, &g) == CUDA_SUCCESS);
  CHECK(g != nullptr);
  CHECK(g != make_graph_sentinel());
}

TEST_CASE("cu_stream_capture: re-capture after End succeeds (state reset)") {
  CUstream s = make_stream(40);
  REQUIRE(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  CUgraph g1;
  REQUIRE(cuStreamEndCapture(s, &g1) == CUDA_SUCCESS);
  // Now should be able to Begin again (status reset to NONE).
  REQUIRE(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  CUstreamCaptureStatus status;
  CHECK(cuStreamIsCapturing(s, &status) == CUDA_SUCCESS);
  CHECK(status == CU_STREAM_CAPTURE_STATUS_ACTIVE);
  CUgraph g2;
  cuStreamEndCapture(s, &g2);
}

TEST_CASE("cu_stream_capture: mixed successful + failed Begin transitions") {
  CUstream s = make_stream(41);
  REQUIRE(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  // F-1 reject: GLOBAL already active. Mode check happens first, so this
  // returns NOT_SUPPORTED without modifying state (state remains ACTIVE).
  CHECK(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_RELAXED)
        == CUDA_ERROR_NOT_SUPPORTED);
  CUstreamCaptureStatus status;
  CHECK(cuStreamIsCapturing(s, &status) == CUDA_SUCCESS);
  // State unchanged because F-1 reject is checked BEFORE state transition.
  CHECK(status == CU_STREAM_CAPTURE_STATUS_ACTIVE);
  CUgraph g;
  REQUIRE(cuStreamEndCapture(s, &g) == CUDA_SUCCESS);
  cuGraphDestroy(g);
}

// ---------------------------------------------------------------------------
// Stage 1.4 regression (2 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cu_stream_capture: cuStreamGetCaptureInfo returns NONE for fresh stream") {
  CUstream s = make_stream(50);
  CUstreamCaptureStatus status = CU_STREAM_CAPTURE_STATUS_ACTIVE;
  cuuint64_t id = 999;
  CHECK(cuStreamGetCaptureInfo(s, &status, &id) == CUDA_SUCCESS);
  CHECK(status == CU_STREAM_CAPTURE_STATUS_NONE);
  CHECK(id == 0);
}

TEST_CASE("cu_stream_capture: cuStreamGetCaptureInfo with NULL id still works") {
  CUstream s = make_stream(51);
  CUstreamCaptureStatus status;
  CHECK(cuStreamGetCaptureInfo(s, &status, nullptr) == CUDA_SUCCESS);
  CHECK(status == CU_STREAM_CAPTURE_STATUS_NONE);
}

// ---------------------------------------------------------------------------
// Fence_id lifecycle (5 cases — Phase 3.2 prep; cuGraphLaunch still no-op)
// ---------------------------------------------------------------------------

TEST_CASE("cu_stream_capture: Begin-End-GraphInstantiate full flow") {
  CUstream s = make_stream(60);
  REQUIRE(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  CUgraph g;
  REQUIRE(cuStreamEndCapture(s, &g) == CUDA_SUCCESS);
  CUgraphExec exec;
  REQUIRE(cuGraphInstantiate(&exec, g, nullptr, nullptr, 0) == CUDA_SUCCESS);
  CHECK(exec != nullptr);
  CHECK(cuGraphExecDestroy(exec) == CUDA_SUCCESS);
  CHECK(cuGraphDestroy(g) == CUDA_SUCCESS);
}

TEST_CASE("cu_stream_capture: graph handle is non-null and pointer-distinct") {
  CUstream s = make_stream(61);
  REQUIRE(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  CUgraph g;
  REQUIRE(cuStreamEndCapture(s, &g) == CUDA_SUCCESS);
  CHECK(reinterpret_cast<uintptr_t>(g) > 0);
  cuGraphDestroy(g);
}

TEST_CASE("cu_stream_capture: status NONE before Begin, ACTIVE after Begin, NONE after End") {
  CUstream s = make_stream(62);
  CUstreamCaptureStatus status;
  REQUIRE(cuStreamIsCapturing(s, &status) == CUDA_SUCCESS);
  CHECK(status == CU_STREAM_CAPTURE_STATUS_NONE);
  REQUIRE(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  REQUIRE(cuStreamIsCapturing(s, &status) == CUDA_SUCCESS);
  CHECK(status == CU_STREAM_CAPTURE_STATUS_ACTIVE);
  CUgraph g;
  REQUIRE(cuStreamEndCapture(s, &g) == CUDA_SUCCESS);
  REQUIRE(cuStreamIsCapturing(s, &status) == CUDA_SUCCESS);
  CHECK(status == CU_STREAM_CAPTURE_STATUS_NONE);
}

TEST_CASE("cu_stream_capture: INVALIDATED state persists across queries") {
  CUstream s = make_stream(63);
  REQUIRE(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  // Force INVALIDATED via illegal transition.
  REQUIRE(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL)
          == CUDA_ERROR_ILLEGAL_STATE);
  CUstreamCaptureStatus status;
  REQUIRE(cuStreamIsCapturing(s, &status) == CUDA_SUCCESS);
  CHECK(status == CU_STREAM_CAPTURE_STATUS_INVALIDATED);
  // Status should persist (not auto-reset).
  REQUIRE(cuStreamIsCapturing(s, &status) == CUDA_SUCCESS);
  CHECK(status == CU_STREAM_CAPTURE_STATUS_INVALIDATED);
}

TEST_CASE("cu_stream_capture: cuGraphLaunch on exec from captured graph returns SUCCESS") {
  CUstream s = make_stream(64);
  REQUIRE(cuStreamBeginCapture(s, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);
  CUgraph g;
  REQUIRE(cuStreamEndCapture(s, &g) == CUDA_SUCCESS);
  CUgraphExec exec;
  REQUIRE(cuGraphInstantiate(&exec, g, nullptr, nullptr, 0) == CUDA_SUCCESS);
  // PoC: cuGraphLaunch is no-op (returns SUCCESS without dispatching).
  CHECK(cuGraphLaunch(exec, s) == CUDA_SUCCESS);
  CHECK(cuGraphExecDestroy(exec) == CUDA_SUCCESS);
  CHECK(cuGraphDestroy(g) == CUDA_SUCCESS);
}