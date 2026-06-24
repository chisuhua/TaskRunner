#include "test_fixture/TaskRunner.h"

namespace async_task {

// Definition of thread_local variable
thread_local std::unique_ptr<CmdStream> TaskRunner::cmdStream_ = nullptr;

} // namespace async_task
