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
    bool register_(const char *token, size_t expireInSeconds, const char *account); // return false is already exists
    MxError authorize(const char *token) const;
    std::string getAccount(const char *token) const;
    void logout(const char *token);

    // --- well-known ---
    void setWellKnownCacheTime(u64 ms) { _wellKnownValidTime = ms; }
    void setWellKnownFailCacheTime(u64 ms) { _wellKnownFailTime = ms; }
    void storeHomeserverForHost(const char *host, const char *hs, unsigned port);
    void storeFailForHost(const char *host);
    LookupResult getCachedHomeserverForHost(const char *host, std::string& hsOut, unsigned& portOut) const;

    // --- hash pepper (NOT zero-terminated!) ---
    enum { HASH_PEPPER_BUFSIZE = 20 };
    static std::string GenerateHashPepper(size_t len);
    std::string getHashPepper(bool allowUpdate);
    void rotateHashPepper();

    bool save(const char *fn) const;
    bool load(const char *fn);

private:
    bool save_nolock(const char *fn) const;
    bool load_nolock(const char *fn);
    void rotateHashPepper_nolock(u64 now);


    DataTree authdata;
    DataTree wellknown;
    u64 _wellKnownValidTime, _wellKnownFailTime;

    std::string hashPepper;
    u64 hashPepperTime; // timestamp at which the pepper was generated
    u64 hashPapperValidity;
    size_t hashPepperLenMin, hashPepperLenMax;
    std::mutex hashPepperMtx;

};
