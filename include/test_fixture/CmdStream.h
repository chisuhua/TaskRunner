#ifndef CMD_STREAM_H
#define CMD_STREAM_H

#include "test_fixture/CmdBuffer.h"
#include "test_fixture/EventQueue.h"

// Forward declaration to avoid circular dependency
namespace async_task {
    class TaskRunner;
}

namespace async_task {

class CmdStream {
public:
    CmdStream(bool isOrdered) {
        cmdBuffer_ = std::make_unique<CmdBuffer>(isOrdered);
    }

    template<typename R>
    std::future<R> launch(std::function<R()> task) {
        auto promise = std::make_shared<std::promise<R>>();
        auto future = promise->get_future();
        cmdBuffer_->emplace([task, promise]() {
            try {
                promise->set_value(task());
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        });
        eventQueue_.push(Event{Event::TASK, cmdBuffer_.get(), []{}});
        return future;
    }

    void launch(Task task) {
        cmdBuffer_->emplace(std::move(task));
        eventQueue_.push(Event{Event::TASK, cmdBuffer_.get(), []{}});
    }

    void launch(CmdBuffer& cmdBuffer) {
        cmdBuffer_->emplace(cmdBuffer);
        eventQueue_.push(Event{Event::TASK, cmdBuffer_.get(), []{}});
    }

    void launch(Barrier& barrier) {
        cmdBuffer_->emplace(barrier);
        eventQueue_.push(Event{Event::TASK, cmdBuffer_.get(), []{}});
    }

    CmdBuffer* getCmdBuffer() {
        return cmdBuffer_.get();
    }

    EventQueue& getEventQueue() {
        return eventQueue_;
    }

    // Make cmdBuffer_ accessible for TaskRunner
    std::unique_ptr<CmdBuffer> cmdBuffer_;

private:
    EventQueue eventQueue_;
};

} // namespace async_task

#endif // CMD_STREAM_H

