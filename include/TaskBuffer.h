#ifndef TASK_BUFFER_HPP
#define TASK_BUFFER_HPP

#include <stack>
#include <mutex>
#include <functional>

namespace async_task_system {

class TaskBuffer {
public:
    void push(Task task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(std::move(task));
    }

    Task pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tasks_.empty()) return nullptr;
        Task task = std::move(tasks_.top());
        tasks_.pop();
        return task;
    }

    bool isEmpty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.empty();
    }

private:
    std::stack<Task> tasks_;
    mutable std::mutex mutex_;
};

} // namespace async_task_system

#endif // TASK_BUFFER_HPP

