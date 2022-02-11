#pragma once

// Extra constructs for multithreading

#include <thread>
#include <condition_variable>
#include <mutex>

class AutoResetEvent
{
public:
    AutoResetEvent();
    void wait();
    void signal();
private:
    AutoResetEvent(const AutoResetEvent&) = delete;
    std::condition_variable _cond;
    std::mutex _mtx;
    int _status;
};
