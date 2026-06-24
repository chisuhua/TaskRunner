#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

#include <queue>
#include <mutex>
#include <variant>
#include <functional>
#include <optional>

namespace async_task {

using Task = std::function<void()>;
using TaskBuffer = std::deque<Task>;

class TaskQueue {
public:
    TaskQueue() {}

    void push(Task task) {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_.push(std::move(task));
    }

    void push(TaskBuffer taskBuffer) {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_.push(std::move(taskBuffer));
    }

    std::optional<std::variant<Task, TaskBuffer>> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        auto task = std::move(queue_.front());
        queue_.pop();
        return task;
    }

    bool isEmpty() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    // Provide access to mutex_ for external locking (needed by CmdProcessor)
    std::mutex& getMutex() { return mutex_; }
    
    // Provide access to queue_ for stealing (needed by CmdProcessor)
    std::queue<std::variant<Task, TaskBuffer>>& getQueue() { return queue_; }
    
    bool isEmptyInternal() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    std::queue<std::variant<Task, TaskBuffer>> queue_;
    mutable std::mutex mutex_;
};

} // namespace async_task

#endif // TASK_QUEUE_H

