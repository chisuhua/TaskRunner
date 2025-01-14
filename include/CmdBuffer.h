#ifndef CMD_BUFFER_H
#define CMD_BUFFER_H

#include <queue>
#include <variant>
#include <functional>
#include <memory>
#include <future>
#include <map>
#include "Barrier.h"  // Ensure Barrier is defined

// Forward declaration of Future
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
                        TaskRunner::getInstance().checkFenceValue(); // Check for paused CmdBuffers
                    }
                }
            }
        }
    }

    void launch() {
        if (!isLaunched_) {
            TaskRunner::getInstance().launch(*this);
            isLaunched_ = true;
        }
    }

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

    std::optional<std::variant<Task*, Barrier*, CmdBuffer*>> getTask() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (buffer_.empty()) return std::nullopt;
        return std::move(buffer_.front());
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
    std::queue<std::variant<Task*, Barrier*, CmdBuffer*>> buffer_;
    mutable std::mutex mutex_;
    std::map<int, std::shared_ptr<std::promise<bool>>> fencePromises_;
    std::map<int, int> groupPendingCmdCnt_; // Group ID to pending command count
    std::shared_ptr<std::promise<bool>> waitingPromise_; // Current waiting promise
};

} // namespace async_task

#endif // CMD_BUFFER_H

