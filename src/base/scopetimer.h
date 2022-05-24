#pragma once

#include "types.h"
#include <chrono>

struct ScopeTimer
{
    typedef std::chrono::high_resolution_clock::time_point TP;
    const TP t0;

    ScopeTimer()
        : t0(std::chrono::high_resolution_clock::now())
    {}

    template<typename U>
    u64 diff() const
    {
        TP t1 = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<U>(t1-t0).count();
    }
    u64 ns() const
    {
        return this->diff<std::chrono::nanoseconds>();
    }
    u64 ms() const
    {
        return this->diff<std::chrono::milliseconds>();
    }
};
