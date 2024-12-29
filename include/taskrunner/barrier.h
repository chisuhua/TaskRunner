#pragma once

#include <atomic>
#include <cstddef> // Include cstddef for size_t

struct Barrier {
    std::atomic<std::size_t> expect_arrive;
    std::atomic<std::size_t> arrive_cnt;
    std::atomic<std::size_t> expect_wait;
    std::atomic<std::size_t> wait_cnt;
    std::atomic<bool> arrive_phase;
    std::atomic<bool> wait_phase;

    Barrier() : expect_arrive(0), arrive_cnt(0),
                expect_wait(0), wait_cnt(0),
                arrive_phase(false), wait_phase(false) {}

    Barrier(std::size_t expect_arrive, std::size_t expect_wait)
        : expect_arrive(expect_arrive), arrive_cnt(0),
          expect_wait(expect_wait), wait_cnt(0),
          arrive_phase(false), wait_phase(false) {}

    void arrive(size_t bar_id);
    void wait(size_t bar_id);
};
