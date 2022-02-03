#pragma once

#include "types.h"
#include <vector>

struct _CoroOpaque;
typedef _CoroOpaque* Coro;

struct CoResult
{
    size_t ret;
    bool alive;
};

typedef void(*CoFunc)(void *args, size_t k);

Coro co_new(CoFunc func, void *args);
void co_delete(Coro c);
size_t co_yield0();
size_t co_yield1(size_t x);
CoResult co_resume1(Coro c, size_t x);


// Protocol:
// Schedule task f to run in xx ms, or to run at a time point (now() + xx ms)
// Once it's time to run f, it will be called with the args pointer and the
// ms it was called too late in k.
// f may j = co_yield1(N) to be resumed in N+j, where j is the jitter that
// the call is too late.
class CoroRunner
{
public:
    CoroRunner();
    ~CoroRunner();

    // relative time delay in ms
    void scheduleIn(CoFunc f, void *args, u64 delay);

    // absolute point in time
    void scheduleAt(CoFunc f, void *args, u64 when);

    // returns time in ms until the next coro is ready to run,
    // 0 when there is nothing to do
    u64 update(u64 now);

    void killAll();
    size_t alive() const { return tasks.size(); }

private:
    struct Task
    {
        Coro c;
        size_t when;
    };
    std::vector<Task> tasks;
};
