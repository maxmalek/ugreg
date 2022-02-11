#include "co.h"
#include "minicoro.h"
#include "util.h"
#include <assert.h>

#define $C ((mco_coro*)(void*)c)

inline static mco_result CHECK(mco_result x)
{
    assert(x == MCO_SUCCESS);
    return x;
}

struct CoPass
{
    CoFunc func;
    void *args;
};

static void _cothunk(mco_coro *co)
{
    CoPass pass = *(CoPass*)mco_get_user_data(co); // copy onto own stack
    size_t k = mco_yield(co); // sync point ---->
    pass.func(pass.args, k);
}

Coro co_new(CoFunc func, void* args)
{
    CoPass pass { func, args };

    mco_desc desc = mco_desc_init(_cothunk, 0);
    desc.user_data = &pass;
    mco_coro* co = 0;
    CHECK(mco_create(&co, &desc));
    assert(co);
    assert(mco_status(co) == MCO_SUSPENDED);

    CHECK(mco_resume(co));
    // sync point <----
    // next resume will run func(args)

    return (Coro)(void*)co;
}

void co_delete(Coro c)
{
    mco_destroy($C);
}

size_t co_yield0(size_t x)
{
    return co_yield1(0);
}

size_t co_yield1(size_t x)
{
    mco_coro *c = mco_running();
    CHECK(mco_push($C, &x, sizeof(x)));
    CHECK(mco_yield($C));
    CHECK(mco_pop($C, &x, sizeof(x)));
    return x;
}

CoResult co_resume1(Coro c, size_t x)
{
    CHECK(mco_push($C, &x, sizeof(x)));
    CHECK(mco_resume($C));
    CHECK(mco_pop($C, &x, sizeof(x)));

    CoResult res { x, res.alive = mco_status($C) == MCO_SUSPENDED };
    return res;
}

CoroRunner::CoroRunner()
{
}

CoroRunner::~CoroRunner()
{
    for(size_t i = 0; i < tasks.size(); ++i)
        co_delete(tasks[i].c);
}

void CoroRunner::scheduleIn(CoFunc f, void* args, u64 delay)
{
    scheduleAt(f, args, timeNowMS() + delay);
}

void CoroRunner::scheduleAt(CoFunc f, void* args, u64 when)
{
    Task t { co_new(f, args), when };
    tasks.push_back(t);
}

u64 CoroRunner::update(u64 now)
{
    u64 next = u64(-1);
    for (size_t i = 0; i < tasks.size();)
    {
        if(now < tasks[i].when)
            next = std::min(next, tasks[i++].when - now);
        else
        {
            u64 late = now - tasks[i].when;
            CoResult r = co_resume1(tasks[i].c, late);
            if(r.alive)
            {
                tasks[i++].when = now + r.ret;
                next = std::min(next, r.ret);
            }
            else
            {
                tasks[i] = tasks.back();
                tasks.pop_back();
            }
        }
    }
    return next;
}

void CoroRunner::killAll()
{
    for(size_t i = 0; i < tasks.size(); ++i)
        co_delete(tasks[i].c);
    tasks.clear();
}
