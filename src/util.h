#pragma once

#include "types.h"

struct NumConvertResult
{
    size_t used; // number of chars processed
    bool overflow;

    // this is probably what you want to check for
    inline bool ok() const { return used && !overflow; }
};

// for not necessarily \0-terminated strings. always writes dst.
// pass maxlen == -1 to stop at \0, otherwise after maxlen chars.
NumConvertResult strtosizeNN(size_t *dst, const char *s, size_t maxlen = -1);

// converts a time value to milliseconds and stores in dst.
// accepted suffixes: d, h, m, s, ms
// so something like 2h30m5s is valid. spaces are invalid.
// s may or may not be \0-terminated.
NumConvertResult strToDurationMS_NN(u64 *dst, const char *s, size_t maxlen = -1);

// safe version that checks that exactly maxlen bytes were consumed
// (or if -1, that the end of the string was hit).
bool strToDurationMS_Safe(u64* dst, const char* s, size_t maxlen = -1);


u64 timeNowMS();

unsigned getNumCPUCores();

// Sleep calling thread for some amount of ms.
// Returns how long the sleep actually took (to account for variations in scheduling, etc)
u64 sleepMS(u64 ms);
