#pragma once

#include "types.h"
#include <limits>

#define ASOCN_SPONGE_DECL_ONLY
#include "ascon-sponge.h"


class SplitMix64
{
public:
    SplitMix64(u64 seed);
    u64 next();
private:
    u64 x;
};

class AsconRand
{
public:
    AsconRand();
    u64 next();
    u64 next(u64 duplex); // mix duplex value back into the state
    void absorb(const u64 *seed, size_t n);
private:
    ascon_state_t state;
};

class MixRand
{
public:
    MixRand();
    void systemSeed();
    void absorb(const u64 *seed, size_t n);
    u64 next();
    u64 next(u64 duplex); // mix duplex value back into the state
private:
    AsconRand ascon;
};

MixRand& GetThreadRng();
u64 GetRandom64();
u64 GetRandom64(u64 duplex); // mix duplex value back into the state

// adapter for std::uniform_int_distribution
struct RngEngine
{
    typedef u64 result_type;
    MixRand& _r;
    inline RngEngine(MixRand& r) : _r(r) {}
    inline RngEngine() : RngEngine(GetThreadRng()) {}
    inline result_type operator()() { return _r.next(); }

    static constexpr result_type min() { return std::numeric_limits<result_type>::min(); }
    static constexpr result_type max() { return std::numeric_limits<result_type>::max(); }
};

int RandomNumberBetween(int minval, int maxval); // inclusive
