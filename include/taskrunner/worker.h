#pragma once

#include <thread>
#include "scheduler.h"

class Scheduler;

class Worker {
public:
    Worker(size_t id, Scheduler& scheduler);

    void start();

private:
    size_t id_;
    Scheduler& scheduler_;
    std::thread thread_;

    void operator()();

friend class Scheduler;
};
