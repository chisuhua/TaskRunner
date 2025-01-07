#ifndef TASK_QUEUE_HPP
#define TASK_QUEUE_HPP

#include <queue>
#include <mutex>
#include <variant>
#include <functional>

namespace async_task_system {

using Task = std::function<void()>;

class TaskQueue {
public:
    void push(Task task) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::variant<Task, TaskBuffer>(std::move(task)));
    }

    void push(TaskBuffer taskBuffer) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::variant<Task, TaskBuffer>(std::move(taskBuffer)));
    }

    std::variant<Task, TaskBuffer> pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::variant<Task, TaskBuffer>{};
        std::variant<Task, TaskBuffer> task = std::move(queue_.front());
        queue_.pop();
        return task;
    }

    bool isEmpty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    std::queue<std::variant<Task, TaskBuffer>> queue_;
    mutable std::mutex mutex_;
};

} // namespace async_task_system

#endif // TASK_QUEUE_HPP

