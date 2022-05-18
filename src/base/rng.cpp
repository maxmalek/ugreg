#include "rng.h"
#include <stdlib.h>
#include <time.h>
#include <random>
#include "util.h"

#ifdef _WIN32
#include <Windows.h>
#include <intrin.h>
#else
#include <sys/random.h>
#endif

#ifdef _MSC_VER
#pragma warning(disable:4996) // 'GetVersion': was declared deprecated
#endif

SplitMix64::SplitMix64(u64 seed)
    : x(seed)
{
}

u64 SplitMix64::next()
{
    u64 z = (x += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

AsconRand::AsconRand()
{
    ascon_sponge_init(&state);
}

u64 AsconRand::next()
{
    return ascon_sponge_squeeze_block(&state);
}

u64 AsconRand::next(u64 duplex)
{
    return ascon_sponge_duplex_block(&state, duplex);
}

void AsconRand::absorb(const u64* seed, size_t n)
{
    ascon_sponge_absorb_blocks(&state, seed, n);
}

MixRand::MixRand()
{
}

void MixRand::systemSeed()
{
    {
        u64 a[] = { u64(rand()), u64(rand()), u64(rand()), u64(time(NULL)) };
        ascon.absorb(a, Countof(a));
    }

    try
    {
        std::random_device rd;
        u64 a[16];
        for(size_t i = 0; i < Countof(a); ++i)
            a[i] = u64(rd());
        ascon.absorb(a, Countof(a));
    }
    catch(...) {};


#ifdef _WIN32
    LARGE_INTEGER pc, pf;
    SYSTEMTIME t;
    QueryPerformanceCounter(&pc);
    QueryPerformanceFrequency(&pf);
    GetLocalTime(&t);
    if(LPCH const e = GetEnvironmentStrings())
    {
        size_t k = 0;
        for(;; ++k)
            if(!(e[k] | e[k+1]))
                break;
        ascon.absorb((u64*)e, k / sizeof(u64));
        FreeEnvironmentStrings(e);
    }

    u64 sys[] =
    {
#if defined(_M_X86) || defined(_M_X64) || defined(_M_X86_64)
        __rdtsc(),
#endif
        u64(pc.QuadPart),
        u64(pf.QuadPart),
        u64(t.wYear),
        u64(t.wMonth),
        u64(t.wDayOfWeek),
        u64(t.wDay),
        u64(t.wHour),
        u64(t.wMinute),
        u64(t.wSecond),
        u64(t.wMilliseconds),
        u64(GetCurrentThreadId()),
        u64(GetCurrentProcessId()),
        u64(GetCurrentProcessorNumber()),
        u64(GetTickCount64()),
        u64(GetVersion()),
        u64((uintptr_t)GetVersion)
    };

#else
    u64 sys[16];
    getrandom(buf, sizeof(buf), 0);
#endif
    ascon.absorb(sys, Countof(sys));
}

void MixRand::absorb(const u64* seed, size_t n)
{
    ascon.absorb(seed, n);
}

u64 MixRand::next(u64 duplex)
{
    return ascon.next(duplex);
}

u64 MixRand::next()
{
    return ascon.next();
}

struct PerThreadRng
{
    MixRand rng;
    unsigned inited = 0;
};

static thread_local PerThreadRng s_th;

MixRand& GetThreadRng()
{
    PerThreadRng & r = s_th;
    if(!r.inited)
    {
        r.rng.systemSeed();
        r.inited = 1;
    }
    return r.rng;
}

u64 GetRandom64()
{
    return GetThreadRng().next();
}

u64 GetRandom64(u64 duplex)
{
    return GetThreadRng().next(duplex);
}
