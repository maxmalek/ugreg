#pragma once

#include "types.h"

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
