#ifndef EVENT_QUEUE_H
#define EVENT_QUEUE_H

#include <queue>
#include <variant>
#include <functional>
#include <condition_variable>
#include <mutex>

namespace async_task {

struct Event {
    enum Type { TASK, TODO_OTHERS };
    Type type;
    CmdBuffer* activeQueue;
    std::function<void()> callback;
};

class EventQueue {
public:
    EventQueue() {}

    void push(Event event) {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_.push(std::move(event));
        cond_var_.notify_one();
    }

    Event pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_var_.wait(lock, [this] { return !queue_.empty(); });
        Event event = std::move(queue_.front());
        queue_.pop();
        return event;
    }

    bool isEmpty() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    std::queue<Event> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_var_;
};

} // namespace async_task

#endif // EVENT_QUEUE_H

