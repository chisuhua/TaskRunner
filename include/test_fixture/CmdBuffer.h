#ifndef CMD_BUFFER_H
#define CMD_BUFFER_H

#include <deque>
#include <variant>
#include <functional>
#include <memory>
#include <future>
#include <map>
#include <optional>
#include "Barrier.h"  // Ensure Barrier is defined

// Forward declaration of Future (TaskRunner will be included at the end)
namespace async_task {
    template<typename R>
    class Future;
}

namespace async_task {

using Task = std::function<void()>;

class CmdBuffer;

class CmdBuffer {
public:
    CmdBuffer(bool isOrdered) : isOrdered_(isOrdered), isLaunched_(false) {
        groupPendingCmdCnt_[0] = 0; // Default group ID is 0
    }

    void setOrdered(bool isOrdered) {
        isOrdered_ = isOrdered;
    }

    bool isOrdered() const {
        return isOrdered_;
    }

    void setFenceValue(std::shared_ptr<std::promise<bool>> promise, int groupId) {
        std::lock_guard<std::mutex> lock(mutex_);
        fencePromises_[groupId] = promise;
    }

    void setWaitingPromise(std::shared_ptr<std::promise<bool>> promise) {
        std::lock_guard<std::mutex> lock(mutex_);
        waitingPromise_ = promise;
    }

    std::shared_ptr<std::promise<bool>> getWaitingPromise() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return waitingPromise_;
    }

    void addPendingCmdCnt(int groupId) {
        std::lock_guard<std::mutex> lock(mutex_);
        groupPendingCmdCnt_[groupId] = 0; // Initialize new group ID
    }

    void incrementPendingCmdCnt() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [groupId, count] : groupPendingCmdCnt_) {
            count++;
        }
    }

    void decrementPendingCmdCnt() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [groupId, count] : groupPendingCmdCnt_) {
            if (count > 0) {
                count--;
                if (count == 0) {
                    if (fencePromises_.count(groupId) > 0) {
                        auto promise = fencePromises_[groupId];
                        promise->set_value(true);
                        fencePromises_.erase(groupId);
                        // TaskRunner dependency - will be implemented after TaskRunner is defined
                        checkFenceValueImpl();
                    }
                }
            }
        }
    }

    void launch() {
        if (!isLaunched_) {
            // TaskRunner dependency - will be implemented after TaskRunner is defined
            launchImpl();
            isLaunched_ = true;
        }
    }
    
    // Getter methods
    bool isLaunched() const {
        return isLaunched_;
    }
    
    template<typename R>
    std::future<R> getFuture() {
        auto promise = std::make_shared<std::promise<R>>();
        auto future = promise->get_future();
        buffer_.emplace_back(Task([promise] {
            // Placeholder task for future retrieval
            promise->set_value(R{});
        }));
        return future;
    }
    
    // Implementation methods for TaskRunner dependencies (defined after TaskRunner class)
    void checkFenceValueImpl();
    void launchImpl();

    template<typename R, typename... Args>
    Future<R> emplace(Args&&... args) {
        auto promise = std::make_shared<std::promise<R>>();
        auto future = promise->get_future();
        buffer_.emplace_back(Task([task = std::bind(std::forward<Args>(args)...), promise] {
            try {
                R result = task();
                promise->set_value(result);
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        }));
        return Future<R>(std::move(future), this);
    }

    template<typename... Args>
    void emplace(void (*task)(Args...), Args... args) {
        buffer_.emplace_back(Task([task, args...] {
            task(args...);
        }));
    }

    void emplace(Task task) {
        buffer_.emplace_back(std::move(task));
    }

    void emplace(Barrier& barrier) {
        buffer_.emplace_back(barrier);
    }

    void emplace(CmdBuffer& cmdBuffer) {
        buffer_.emplace_back(std::ref(cmdBuffer));
    }
    
    void emplace(CmdBuffer* cmdBuffer) {
        buffer_.emplace_back(std::ref(*cmdBuffer));
    }

    std::optional<std::variant<Task, Barrier, std::reference_wrapper<CmdBuffer>>> getTask() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (buffer_.empty()) return std::nullopt;
        return buffer_.front();
    }

    void removeTask() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!buffer_.empty()) buffer_.pop_front();
    }

    bool isEmpty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_.empty();
    }

    std::map<int, std::shared_ptr<std::promise<bool>>> getFencePromises() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return fencePromises_;
    }

private:
    bool isOrdered_;
    bool isLaunched_;
    std::deque<std::variant<Task, Barrier, std::reference_wrapper<CmdBuffer>>> buffer_;
    mutable std::mutex mutex_;
    std::map<int, std::shared_ptr<std::promise<bool>>> fencePromises_;
    std::map<int, int> groupPendingCmdCnt_; // Group ID to pending command count
    std::shared_ptr<std::promise<bool>> waitingPromise_; // Current waiting promise
};

} // namespace async_task

// Forward declarations for methods that need TaskRunner
// The actual include will come from files that include both CmdBuffer.h and TaskRunner.h
#ifndef TASK_RUNNER_H_INCLUDED
namespace async_task {
    class TaskRunner;  // Forward declare - full definition must be available when using methods that call TaskRunner
}
#endif

#endif // CMD_BUFFER_H

