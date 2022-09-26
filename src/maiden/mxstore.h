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

    // -- update --
    void merge3pid(VarCRef root);
    void merge3pid_nolock(VarCRef root);
    DataTree::LockedRoot get3pidRoot();
    void _clearHashCache_nolock();
    void markForRehash_nolock();


    bool save(const char *fn) const;
    bool load(const char *fn);

    struct SearchConfig
    {
        struct Field
        {
            bool fuzzy = false;
        };
        typedef std::unordered_map<std::string, Field> Fields;
        Fields fields;
        std::string displaynameField;

        // -- below here is not used by mxstore --
        std::string avatar_url;
        size_t maxsize = 1024; // max. size of search request, json and all
    };

    struct SearchResult
    {
        std::string str, displayname;
        int score;

        // highest score first, then alphabetically
        inline bool operator<(const SearchResult& o) const
        {
            return score > o.score || (score == o.score && str < o.str);
        }
    };

    MxError search(std::vector<SearchResult>& results, const SearchConfig& scfg, const char *term);

private:
    bool save_nolock(const char *fn) const;
    bool load_nolock(const char *fn);
    std::string getHashPepper_nolock(bool allowUpdate);
    void rotateHashPepper_nolock(u64 now);
    MxError _generateHashCache_nolock(VarRef cache, const char *algo);
    MxError unhashedFuzzyLookup_nolock(VarRef dst, VarCRef in); // only for algo == "none"

    // dst becomes { 3pid => mxid }, where src is a list of { mxid => { ..., <fromkey>=3pid, ... }
    // Actually src is the large user-to-data table returned by an import script, and ffromkey is the key under which to look up
    // and entry that is to be used as a medium; so dst is likely some map stored under <medium> as a key, but the caller has to take care of that
    static void _Rebuild3pidMap(VarRef dst, VarCRef src, const char* fromkey);


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

        typedef std::unordered_map<std::string, std::string> Media;
        Media media; // field => medium

    };
    const Config& getConfig() const { return this->config; }

private:
    Config config;

};
