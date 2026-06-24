// SCOPE: UMD-EVOLUTION
// STATUS: PROPOSED
// IMPLEMENTED: NO

#include "umd/ring_buffer.hpp"

namespace async_task::umd {

void* RingBuffer::create(std::size_t entry_count, std::size_t entry_size) {
    (void)entry_count; (void)entry_size;
    return nullptr;
}

int RingBuffer::submit(void* ring, const void* entry) {
    (void)ring; (void)entry;
    return -38;  // -ENOSYS
}

std::uint64_t RingBuffer::ring_doorbell(void* doorbell_ptr, std::uint64_t value) {
    (void)doorbell_ptr; (void)value;
    return 0;
}

}  // namespace async_task::umd
