CmdBuffer.hpp

#ifndef CMD_BUFFER_HPP
#define CMD_BUFFER_HPP

#include <deque>
#include <atomic>
#include <memory>
#include <variant>
#include <functional>

namespace async_task_system {

using Task = std::function<void()>;

class CmdBuffer {
public:
    CmdBuffer(bool isOrdered) : isOrdered_(isOrdered), pendingCmdCnt_(0), fenceValue_(false) {}
    CmdBuffer(const CmdBuffer&) = delete;
    CmdBuffer& operator=(const CmdBuffer&) = delete;
    CmdBuffer(CmdBuffer&& other) noexcept : tasks_(std::move(other.tasks_)), isOrdered_(other.isOrdered_), pendingCmdCnt_(other.pendingCmdCnt_.load()), fenceValue_(other.fenceValue_), fencePromise_(std::move(other.fencePromise_)) {}
    CmdBuffer& operator=(CmdBuffer&& other) noexcept {
        if (this != &other) {
            tasks_ = std::move(other.tasks_);
            isOrdered_ = other.isOrdered_;
            pendingCmdCnt_ = other.pendingCmdCnt_.load();
            fenceValue_ = other.fenceValue_;
            fencePromise_ = std::move(other.fencePromise_);
        }
        return *this;
    }

    template<typename T, typename... Args>
    void emplace(Args&&... args) {
        tasks_.emplace_back(T(std::forward<Args>(args)...));
    }

    std::optional<std::variant<Task, Barrier&, CmdBuffer&>> getTask() {
        if (tasks_.empty()) return std::nullopt;
        return std::move(tasks_.front());
    }

    void removeTask() {
        if (!tasks_.empty()) tasks_.pop_front();
    }

    bool isEmpty() const {
        return tasks_.empty();
    }

    bool isOrdered() const {
        return isOrdered_;
    }

    void incrementPendingCmdCnt() {
        ++pendingCmdCnt_;
    }

    void decrementPendingCmdCnt() {
        --pendingCmdCnt_;
        if (pendingCmdCnt_ == 0 && fencePromise_) {
            TaskRunner::getInstance().checkFenceValue(this);
        }
    }

    int getPendingCmdCnt() const {
        return pendingCmdCnt_.load();
    }

    void setFenceValue(bool value, std::shared_ptr<std::promise<bool>> promise) {
        fenceValue_ = value;
        fencePromise_ = std::move(promise);
    }

    bool getFenceValue() const {
        return fenceValue_;
    }

    std::shared_ptr<std::promise<bool>> getFencePromise() const {
        return fencePromise_;
    }

private:
    std::deque<std::variant<Task, Barrier&, CmdBuffer&>> tasks_;
    bool isOrdered_;
    std::atomic<int> pendingCmdCnt_;
    bool fenceValue_;
    std::shared_ptr<std::promise<bool>> fencePromise_;
};

} // namespace async_task_system

#endif // CMD_BUFFER_HPP

