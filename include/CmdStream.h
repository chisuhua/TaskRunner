CmdStream.hpp

#ifndef CMD_STREAM_HPP
#define CMD_STREAM_HPP

#include <memory>
#include <variant>
#include <functional>
#include "EventQueue.hpp"
#include "CmdBuffer.hpp"
#include "TaskRunner.hpp"

namespace async_task_system {

class CmdStream {
public:
    CmdStream(bool isOrdered) : isOrdered_(isOrdered), eventQueue_() {
        buffer_ = TaskRunner::getInstance().allocateCmdBuffer(isOrdered);
    }

    template<typename R>
    std::future<R> launch(std::function<R()> task) {
        auto promise = std::make_shared<std::promise<R>>();
        auto future = promise->get_future();
        buffer_->emplace<Task>([task, promise] {
            try {
                R result = task();
                promise->set_value(result);
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        });
        triggerEvent();
        return future;
    }

    void launch(Task task) {
        buffer_->emplace<Task>(std::move(task));
        triggerEvent();
    }

    void launch(CmdBuffer cmdBuffer) {
        buffer_->emplace<CmdBuffer&>(cmdBuffer);
        triggerEvent();
    }

    void launch(Barrier& barrier) {
        buffer_->emplace<Barrier&>(barrier);
        triggerEvent();
    }

    void triggerEvent() {
        eventQueue_.push({Event::TASK, buffer_.get(), []{}});
    }

private:
    bool isOrdered_;
    EventQueue eventQueue_;
    std::unique_ptr<CmdBuffer> buffer_;
};

} // namespace async_task_system

#endif // CMD_STREAM_HPP

