#pragma once

#include "mxdefines.h"
#include "mxsearchalgo.h"
#include <string>
#include "variant.h"
#include <unordered_map>
#include <vector>
#include <mutex>
#include "mem.h"
#include "mxvirtual.h"

class TwoWayMatcher;

struct MxSearchConfig
{
    struct Field // Affects how the search cache is generated from each field
    {
        //bool fuzzy = false; // No longer possible because we're not searching individual fields anymore
    };
    typedef std::unordered_map<std::string, Field> Fields;
    Fields fields;
    std::string displaynameField;

    // -- below here is not used by mxstore --
    std::string avatar_url;
    size_t maxsize = 1024; // max. size of search request, json and all
    bool fuzzy = false;
    bool element_hack = false;
};


class MxSearch : public EvTreeRebuilt
{
public:
    MxSearch(const MxSearchConfig& scfg);
    ~MxSearch();
    bool init(VarCRef cfg);

    void rebuildCache(const MxStore& mxs);


    // First step is to search in the prepared cache.
    // This gives a list of matches, that then need to be resolved
    // using the MxStore to get the actual user names.
    // This minimizes the time the MxStore spends in a locked state.
    struct Match
    {
        StrRef key; // key in mxstore user table
        int score;

        // highest score first
        inline bool operator<(const Match& o) const
        {
            return score > o.score;
        }
    };

    typedef std::vector<Match> Matches;

    Matches search(const MxMatcherList& matchers, bool fuzzy) const;

    // Inherited via EvTreeRebuilt
    virtual void onTreeRebuilt(const MxStore& mxs) override;

private:

    void clear();

    mutable std::shared_mutex mutex;
    // These strings are unique; makes no sense to throw them into a string pool
    // They also contain embedded \0 but are not \0-terminated,
    // so the exact length is recorded for a reason
    std::vector<MutStr> _strings;
    std::vector<StrRef> _keys;
    BlockAllocator _stralloc;
    const MxSearchConfig& scfg;
};

