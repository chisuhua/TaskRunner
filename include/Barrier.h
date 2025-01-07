#ifndef BARRIER_HPP
#define BARRIER_HPP

#include <memory>
#include <future>

namespace async_task_system {

using BarrierType = enum { RELEASE, ACQUIRE, WAIT };

struct Barrier {
    BarrierType type;
    std::shared_ptr<std::promise<bool>> promise;
    bool value;
};

} // namespace async_task_system

#endif // BARRIER_HPP

