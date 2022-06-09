#pragma once

#include <string>
#include "datatree.h"
#include "mxdefines.h"
#include "cachetable.h"
#include <unordered_map>

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

    bool apply(VarCRef config);
    void defrag();

    // --- authentication ---
    bool register_(const char *token, size_t tokenLen, size_t expireInMS, const char *account); // return false is already exists
    MxError authorize(const char *token) const;
    std::string getAccount(const char *token) const;
    void logout(const char *token);

    // --- well-known ---
    void storeHomeserverForHost(const char *host, const char *hs, unsigned port);
    void storeFailForHost(const char *host);
    LookupResult getCachedHomeserverForHost(const char *host, std::string& hsOut, unsigned& portOut) const;

    // --- hash pepper (NOT zero-terminated!) ---
    enum { HASH_PEPPER_BUFSIZE = 20 };
    static std::string GenerateHashPepper(size_t len);
    std::string getHashPepper(bool allowUpdate);
    void rotateHashPepper();

    // -- lookup API --
    MxError hashedBulkLookup(VarRef dst, VarCRef in, const char *algo, const char *pepper); // dst is made a map, in is an array


    bool merge3pid(VarCRef root); // expects { medium => { something => mxid } }

    bool save(const char *fn) const;
    bool load(const char *fn);

private:
    bool save_nolock(const char *fn) const;
    bool load_nolock(const char *fn);
    std::string getHashPepper_nolock(bool allowUpdate);
    void rotateHashPepper_nolock(u64 now);
    MxError _generateHashCache_nolock(VarRef cache, const char *algo);
    void _clearHashCache_nolock();
    MxError unhashedFuzzyLookup_nolock(VarRef dst, VarCRef in); // only for algo == "none"


    DataTree authdata;
    DataTree wellknown;
    DataTree hashcache; // {base64(hash) => mxid} // TODO: maybe don't store the mxid here, it's a duplicate and wastes mem
    DataTree threepid; // {medium => {3pid => mxid}}

    std::string hashPepper;
    u64 hashPepperTS; // timestamp at which the pepper was generated

    // --- config --- (same structure as JSON in config file)

public:
    struct Config
    {
        Config();

        struct
        {
            u64 pepperTime;
            size_t pepperLenMin, pepperLenMax;
        } hashcache;

        struct
        {
            u64 cacheTime;
            u64 failTime;
            u64 requestTimeout;
            u64 requestMaxSize;
        } wellknown;

        struct
        {
            u64 maxTime;
        } register_;

        struct Hash
        {
            Hash();
            bool lazy;
        };

        size_t minSearchLen;

        typedef std::unordered_map<std::string, Hash> Hashes;
        Hashes hashes;

    };
    const Config& getConfig() const { return this->config; }

private:
    Config config;

};
