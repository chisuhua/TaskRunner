// SCOPE: UMD-EVOLUTION
// STATUS: PROPOSED
// IMPLEMENTED: NO

#pragma once

#include <cstddef>
#include <cstdint>

namespace async_task::umd {

// Ring Buffer + Doorbell abstraction (PoC only — NOT IMPLEMENTED)
class RingBuffer {
public:
    // Create ring buffer. Returns nullptr (PoC not implemented).
    void* create(std::size_t entry_count, std::size_t entry_size);

    // Submit entry to ring. Returns -ENOSYS (PoC not implemented).
    int submit(void* ring, const void* entry);

    // Ring doorbell. Returns 0 (PoC not implemented).
    std::uint64_t ring_doorbell(void* doorbell_ptr, std::uint64_t value);
};

}  // namespace async_task::umd
