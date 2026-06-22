# TaskRunner

A concurrent task execution framework with support for task queues, workers, and barriers.

## 整体架构
1. TaskRunner
单例模式：确保在整个应用程序中只有一个 TaskRunner 实例。
初始化：创建多个 CmdProcessor 实例，并启动它们。
任务调度：提供 launch 方法来启动任务、命令缓冲区和栅栏。
事件分发：通过 dispatchLoop 方法统一处理 CmdStream 的事件队列，并将任务分发给不同的 CmdProcessor。
任务队列管理：管理任务队列的偷取功能，确保任务在多个 CmdProcessor 之间均匀分布。
2. CmdStream
事件生成：生成任务、命令缓冲区和栅栏的事件，并将这些事件推送到事件队列。
事件队列：每个 CmdStream 有一个 EventQueue，用于存储生成的事件。
3. CmdBuffer
任务存储：存储任务、栅栏和其他命令缓冲区。
状态管理：管理命令缓冲区的状态，如是否已启动、是否有等待的 promise 等。
任务队列：使用 std::queue<std::variant<Task, Barrier, CmdBuffer>> buffer_ 存储任务和栅栏。
4. CmdProcessor
事件处：从 dispatchToProcessorQueue_ 中读取事件并处理任务。
任务执行：处理命令缓冲区中的任务，并将结果写回到 TaskQueue。
回调处理：将需要调用 TaskRunner 的操作打包成事件并通过 processorToDispatchQueue_ 发送回 dispatchLoop。
5. EventQueue
事件存储：存储和管理事件，使用 std::queue<Event> 存储事件。
同步机制：使用 std::mutex 和 std::condition_variable 确保线程安全。
6. TaskQueue
任务存储：存储和管理任务，使用 std::queue<std::variant<Task, TaskBuffer>> 存储任务和任务缓冲区。
同步机制：使用 std::mutex 确保线程安全。
7. Barrier
栅栏类型：定义了四种栅栏类型：RELEASE、ACQUIRE、WAIT 和 GROUP。
栅栏结构：包含栅栏类型、promise 和组 ID。

## 详细设计
### TaskRunner 类
#### 成员变量：

std::vector<std::unique_ptr<CmdProcessor>> cmdProcessors_：存储所有 CmdProcessor 实例。
std::queue<CmdBuffer> idleBuffers_：存储空闲的命令缓冲区。
std::vector<CmdBuffer*> activeQueues_：存储活跃的命令缓冲区。
std::vector<CmdBuffer*> pauseQueues_：存储暂停的命令缓冲区。
std::condition_variable globalCondVar_ 和 std::mutex globalMutex_：用于全局同步。
thread_local std::unique_ptr<CmdStream> cmdStream_：线程局部的 CmdStream 实例。
std::vector<TaskQueue*> allTaskQueues_：存储所有 CmdProcessor 的任务队列。
std::vector<EventQueue> dispatchToProcessorQueues_：从 dispatchLoop 到 CmdProcessor 的事件队列。
std::vector<EventQueue> processorToDispatchQueues_：从 CmdProcessor 到 dispatchLoop 的事件队列。
std::vector<EventQueue*> cmdStreamEventQueues_：存储所有 CmdStream 的事件队列。
std::thread dispatchThread_：运行 dispatchLoop 的线程。
#### 方法：

static TaskRunner& getInstance()：获取 TaskRunner 单例实例。
template<typename R> Future<R> launch(std::function<R()> task)：启动一个返回值的任务。
void launch(Task task)：启动一个无返回值的任务。
void launch(CmdBuffer cmdBuffer)：启动一个命令缓冲区。
void launch(Barrier& barrier)：启动一个栅栏。
Barrier allocateBarrier(BarrierType type, int groupId = 0)：分配一个栅栏。
Barrier allocateBarrier(BarrierType type, std::shared_ptr<std::promise<bool>> promise, int groupId = 0)：分配一个带有 promise 的栅栏。
void addCmdStream(CmdStream& cmdStream)：将 CmdStream 的事件队列添加到 cmdStreamEventQueues_。
void initialize()：初始化 TaskRunner，创建 CmdProcessor 实例并启动它们。
CmdBuffer allocateCmdBuffer(bool isOrdered)：分配一个命令缓冲区。
void ensureCmdStream()：确保 CmdStream 已经创建并添加到 cmdStreamEventQueues_。
void pauseCmdBuffer(CmdBuffer* cmdBuffer, std::shared_ptr<std::promise<bool>> promise = nullptr)：暂停命令缓冲区。
void resumeCmdBuffer(CmdBuffer* cmdBuffer)：恢复命令缓冲区。
void checkFenceValue()：检查暂停的命令缓冲区，如果 promise 已经完成则恢复。
void addToActiveQueues(CmdBuffer* cmdBuffer)：将命令缓冲区添加到活跃队列。
void dispatchLoop()：处理 CmdStream 的事件队列，将任务分发给不同的 CmdProcessor，并处理 CmdProcessor 发回的回调消息。
void handleEvent(Event event)：处理事件并分发给 CmdProcessor。
bool allQueuesEmpty() const：检查所有事件队列是否为空。
### CmdProcessor 类
#### 成员变量：

TaskQueue& taskQueue_：存储任务的队列。
EventQueue& dispatchToProcessorQueue_：从 dispatchLoop 接收事件的队列。
std::vector<TaskQueue*> allTaskQueues_：存储所有 CmdProcessor 的任务队列。
std::thread thread_：运行 eventLoop 的线程。
EventQueue& processorToDispatchQueue_：向 dispatchLoop 发送回调消息的队列。
#### 方法：

void updateAllTaskQueues(const std::vector<TaskQueue*>& allTaskQueues)：更新所有 CmdProcessor 的任务队列。
void start()：启动 CmdProcessor 的 eventLoop。
void stop()：停止 CmdProcessor 的 eventLoop。
void eventLoop()：处理 dispatchToProcessorQueue_ 中的事件，处理任务并处理任务队列的偷取。
void processActiveQueue(CmdBuffer& activeQueue, TaskQueue& taskQueue)：处理命令缓冲区中的任务。
void handleBarrier(BarrierType type, CmdBuffer& activeQueue, Barrier& barrier)：处理不同类型的栅栏。
void processOrderedQueue(CmdBuffer& activeQueue, TaskQueue& taskQueue)：处理有序命令缓冲区中的任务。
void processUnorderedQueue(CmdBuffer& activeQueue, TaskQueue& taskQueue)：处理无序命令缓冲区中的任务。
void work()：处理任务队列中的任务。
bool allQueuesEmpty() const：检查 dispatchToProcessorQueue_ 是否为空。
bool stealWork()：从其他 CmdProcessor 的任务队列中偷取任务。
### CmdBuffer 类
#### 成员变量：

bool isOrdered_：指示命令缓冲区是否有序。
bool isLaunched_：指示命令缓冲区是否已启动。
std::queue<std::variant<Task, Barrier, CmdBuffer>> buffer_：存储任务、栅栏和其他命令缓冲区。
mutable std::mutex mutex_：用于线程全。
std::map<int, std::shared_ptr<std::promise<bool>>> fencePromises_：存储栅栏的 promise。
std::map<int, int> groupPendingCmdCnt_：存储每个组的待处理任务计数。
std::shared_ptr<std::promise<bool>> waitingPromise_：当前等待的 promise。
#### 方法：

void setOrdered(bool isOrdered)：设置命令缓冲区是否有序。
bool isOrdered() const：获取命令缓冲区是否有序。
void setFenceValue(std::shared_ptr<std::promise<bool>> promise, int groupId)：设置栅栏的 promise。
void setWaitingPromise(std::shared_ptr<std::promise<bool>> promise)：设置当前等待的 promise。
std::shared_ptr<std::promise<bool>> getWaitingPromise() const：获取当前等待的 promise。
void addPendingCmdCnt(int groupId)：增加组的待处理任务计数。
void incrementPendingCmdCnt()：增加所有组的待处理任务计数。
void decrementPendingCmdCnt()：减少所有组的待处理任务计数。
void launch()：启动命令缓冲区。
template<typename R, typename... Args> Future<R> emplace(Args&&... args)：在命令缓冲区中插入一个任务并返回 Future。
template<typename... Args> void emplace(void (*task)(Args...), Args... args)：在命令缓冲区中插入一个任务。
std::optional<std::variant<Task, Barrier, CmdBuffer>> getTask()：从命令缓冲区中获取任务。
void removeTask()：从命令缓冲区中移除任务。
bool isEmpty() const：检查命令缓冲区是否为空。
std::map<int, std::shared_ptr<std::promise<bool>>> getFencePromises() const：获取栅栏的 promise。
### CmdStream 类
#### 成员变量：

std::unique_ptr<CmdBuffer> cmdBuffer_：存储命令缓冲区。
EventQueue eventQueue_：存储事件的队列。
#### 方法：

template<typename R> std::future<R> launch(std::function<R()> task)：启动一个返回值的任务。
void launch(Task task)：启动一个无返回值的任务。
void launch(CmdBuffer cmdBuffer)：启动一个命令缓冲区。
void launch(Barrier& barrier)：启动一个栅栏。
CmdBuffer* getCmdBuffer()：获取命令缓冲区。
EventQueue& getEventQueue()：获取事件队列。
### EventQueue 类
#### 成员变量：

std::queue<Event> queue_：存储事件的队列。
mutable std::mutex mutex_：用于线程安全。
std::condition_variable cond_var_：用于同步事件队列的访问。
#### 方法：

void push(Event event)：将事件推入队列并通知等待的线程。
Event pop()：从队列中弹出事件并等待队列非空。
bool isEmpty() const：检查事件队列是否为空。
### TaskQueue 类
#### 成员变量：

std::queue<std::variant<Task, TaskBuffer>> queue_：存储任务和任务缓冲区的队列。
mutable std::mutex mutex_：用于线程安全。
#### 方法：

void push(Task task)：将任务推入队列。
void push(TaskBuffer taskBuffer)：将任务缓冲区推入队列。
std::optional<std::variant<Task, TaskBuffer>> pop()：从队列中弹出任务或任务缓冲区。
bool isEmpty() const：检查任务队列是否为空。
### Barrier 类
#### 成员变量：
BarrierType type：栅栏类型。
std::shared_ptr<std::promise<bool>> promise：栅栏的 promise。
int groupId：栅栏的组 ID。
## 关键点
### 事件队列：

CmdStream 的事件队列由 dispatchLoop 统一检查和分发任务。
每个 CmdProcessor 有一个 dispatchToProcessorQueue_ 用于接收任务，一个 processorToDispatchQueue_ 用于发送回调消息。
### 任务调度：

dispatchLoop 负责从 CmdStream 的事件队列中读取事件，并将任务分发给不同的 CmdProcessor。
CmdProcessor 的 eventLoop 负责从 dispatchToProcessorQueue_ 中读取事件并处理任务。
### 任务队列：

CmdBuffer 的 buffer_ 存储任务、栅栏和其他命令缓冲区。
TaskQueue 存储任务和任务缓冲区，用于任务偷取和任务处理。
### 栅栏处理：

CmdProcessor 处理栅栏时，会根据栅栏类型执行相应的操作，如设置 promise、暂停或恢复命令缓冲区。
### 线程安全：

使用 std::mutex 和 std::condition_variable 确保多线程环境下的线程安全。
## 优势
减少锁争用：通过 dispatchLoop 统一处理 CmdStream 的事件队列，减少了多个 CmdProcessor 同时访问同一队列的锁争用。
任务均衡：dispatchLoop 以轮询方式将任务分发给不同的 CmdProcessor，确保任务在多个处理器之间均匀分布。
灵活的栅栏处理：CmdProcessor 可以处理不同类型的栅栏，确保任务的顺序和同步。

## Getting Started

### Prerequisites

- CMake 3.10+
- Git

### Installation

1. Clone the repository:

    ```sh
    git clone https://github.com/yourusername/TaskRunner.git
    cd TaskRunner
    ```

2. Build the project:

    ```sh
    mkdir build
    cd build
    cmake ..
    make
    ```

3. Run the application:

    ```sh
    ./TaskRunner
    ```

4. Run tests:

    ```sh
    ctest
    ```

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 相关文档

- [AGENTS.md](AGENTS.md) - Agent 开发指南（构建命令、架构、命名规范）
- [plans/sync-plan.md](plans/sync-plan.md) - TaskRunner-UsrLinuxEmu 协调计划
- [UsrLinuxEmu/docs/07-integration/](UsrLinuxEmu/docs/07-integration/) - GPU 联调指南

## 同步状态

| 阶段 | 状态 | 日期 |
|------|------|------|
| Phase 0-1 (S0-S4) | ✅ 完成 | 2026-04-29 |
| Phase 1.5 (S3.5 + S3.1) | ✅ 完成 | 2026-06-17 |
| S5 Architecture foundation (IGpuDriver + DI + Mock + CLI fix) | ✅ 完成 | 2026-06-19 |
| H-3 (Phase 2 VA Space/Queue 真实实现) | ⏳ 待 S5 archive 后激活 | - |
