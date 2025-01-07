EventQueue.hpp

#ifndef EVENT_QUEUE_HPP
#define EVENT_QUEUE_HPP

#include <queue>
#include <mutex>
#include <condition_variable>
#include <variant>
#include <functional>

namespace async_task_system {

struct Event {
    enum Type { TASK, TODO_OTHERS };
    Type type;
    CmdBuffer* activeQueue;
    std::function<void()> callback;
};

class EventQueue {
public:
    void push(Event event) {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_.push(std::move(event));
        cond_var_.notify_one();
    }

    Event pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (queue_.empty()) {
            cond_var_.wait(lock);
        }
        Event event = std::move(queue_.front());
        queue_.pop();
        return event;
    }

    bool isEmpty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    std::queue<Event> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_var_;
};

} // namespace async_task_system

#endif // EVENT_QUEUE_HPP

