#ifndef CMD_STREAM_H
#define CMD_STREAM_H

#include "CmdBuffer.h"
#include "EventQueue.h"

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
        eventQueue_.push({Event::TASK, cmdBuffer_.get(), []{}});
        return future;
    }

    void launch(Task task) {
        cmdBuffer_->emplace(std::move(task));
        eventQueue_.push({Event::TASK, cmdBuffer_.get(), []{}});
    }

    void launch(CmdBuffer cmdBuffer) {
        cmdBuffer_->emplace(std::move(cmdBuffer));
        eventQueue_.push({Event::TASK, cmdBuffer_.get(), []{}});
    }

    void launch(Barrier& barrier) {
        cmdBuffer_->emplace(barrier);
        eventQueue_.push({Event::TASK, cmdBuffer_.get(), []{}});
    }

    CmdBuffer* getCmdBuffer() {
        return cmdBuffer_.get();
    }

    EventQueue& getEventQueue() {
        return eventQueue_;
    }

private:
    std::unique_ptr<CmdBuffer> cmdBuffer_;
    EventQueue eventQueue_;
};

} // namespace async_task

#endif // CMD_STREAM_H

