#pragma once

#include <string>
#include "datatree.h"
#include "mxdefines.h"
#include "cachetable.h"

class MxStore
{
public:
    MxStore();

    enum LookupResult
    {
        UNKNOWN, // not cached
        VALID,   // valid, cached
        EXPIRED, // valid, old values still in cache
        FAILED,  // cached, known invalid
    };

    // --- authentication ---
    void register_(const char *token, size_t expireInSeconds, const char *account);
    MxError authorize(const char *token) const;
    std::string getAccount(const char *token) const;
    void logout(const char *token);

    // --- well-known ---
    void setWellKnownCacheTime(u64 ms) { _wellKnownValidTime = ms; }
    void setWellKnownFailCacheTime(u64 ms) { _wellKnownFailTime = ms; }
    void storeHomeserverForHost(const char *host, const char *hs, unsigned port);
    void storeFailForHost(const char *host);
    LookupResult getCachedHomeserverForHost(const char *host, std::string& hsOut, unsigned& portOut) const;

    bool save();

private:
    DataTree authdata; // string => User* or None
    DataTree wellknown;
    u64 _wellKnownValidTime, _wellKnownFailTime;

};
