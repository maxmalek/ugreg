#include "mt.h"
#include <assert.h>

// via https://github.com/preshing/cpp11-on-multicore/

AutoResetEvent::AutoResetEvent()
    : _status(0)
{
}

void AutoResetEvent::wait()
{
    std::lock_guard<decltype(_mtx)> g(_mtx);
    const int s = _status;
    if(s == 1)
        return;
    assert(s + 1 <= 1);
    _status = s + 1;
    if(s < 0)
        _cond.notify_one();
}

void AutoResetEvent::signal()
{
    std::unique_lock<decltype(_mtx)> lock(_mtx);
    const int s = _status;
    _status = s - 1;
    assert(s <= 1);
    if(s < 1)
        _cond.wait(lock);
}

