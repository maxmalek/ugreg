#pragma once

#include "variant.h"
#include <thread>
#include <atomic>
#include <vector>
#include <condition_variable>
#include <mutex>
#include <future>
#include "mxvirtual.h"
#include "datatree.h"
#include "mxsearch.h"

// Periodic loader for external 3pid sources
class MxSources
{
public:
    MxSources();
    ~MxSources();
    bool initConfig(VarCRef cfg, VarCRef env);
    void initPopulate(bool buildAsync); // load initial data + start bg maintenance thread when that is done

    struct Config
    {
        enum InputType
        {
            IN_EXEC,
            IN_LOAD
        };
        struct InputEntry
        {
            InputType how;
            std::vector<const char*> args; // actual strings are stored in _argstrs[]
            u64 every;
            bool check;
        };
        u64 purgeEvery;
        std::string directory;
        std::vector<InputEntry> list;
    };

    void addListener(EvTreeRebuilt *ev);
    void removeListener(EvTreeRebuilt *ev);

    DataTree::LockedCRef lockedCRef() const { return _merged.lockedCRef(); }
    DataTree::LockedRef  lockedRef()        { return _merged.lockedRef(); }

    struct SearchResult
    {
        std::string str, displayname;
    };

    typedef std::vector<SearchResult> SearchResults;

    // resolve search matches to actual, ready-to-display search results
    SearchResults formatMatches(const MxSearchConfig& scfg, const MxSearch::Match* matches, size_t n, const char* term) const;

    bool load();
    bool save() const;

private:
    bool _checkAll() const;
    bool _checkExec(const Config::InputEntry& e) const;
    std::vector<std::string> _argstrs;
    void _loop_th(bool buildAsync);
    void _loop_th_untilPurge();
    DataTree *_ingestData(const Config::InputEntry& entry) const; // must delete returned ptr

    struct IngestResult
    {
        DataTree *tree = NULL;
        bool loaded = false; // could load data (aka didn't fail)
        bool merged = false; // everything was as expected, merged too
        bool ignored = false;
    };
    // dst is optional. Protocol:
    // - if dst is valid, merge results into dst and return NULL.
    // - if it's NULL, return new tree. Caller must delete it. On fail, return NULL.
    IngestResult _ingestDataAndMerge(DataTree *dst, const Config::InputEntry& entry);
    std::future<IngestResult> _ingestDataAndMergeAsync(DataTree *dst, const Config::InputEntry& entry);

    void _rebuildTree();
    void _sendTreeRebuiltEvent() const;
    void _updateEnv(VarCRef env);
    static void _Loop_th(MxSources *self, bool buildAsync);
    DataTree _merged; // all configured sources merged together. purged every now and then.
    Config _cfg;
    std::thread _th;
    std::atomic<bool> _quit;
    std::condition_variable _waiter;
    std::mutex _waitlock;
    mutable std::mutex _eventlock;
    std::vector<std::string> _envStrings;
    std::vector<const char*> _envPtrs;
    std::vector<EvTreeRebuilt*> _evRebuilt;
};

