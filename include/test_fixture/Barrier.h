// SCOPE: TEST-FIXTURE
#ifndef BARRIER_H
#define BARRIER_H

#include <memory>
#include <future>

namespace async_task {

enum BarrierType { RELEASE, ACQUIRE, WAIT, GROUP };

struct Barrier {
    BarrierType type;
    std::shared_ptr<std::promise<bool>> promise;
    int groupId;
};

} // namespace async_task

#endif // BARRIER_H

