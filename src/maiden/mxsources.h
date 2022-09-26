#pragma once

#include "variant.h"
#include <thread>
#include <atomic>
#include <vector>
#include <condition_variable>
#include <mutex>
#include <future>

class MxStore;
class DataTree;

// Periodic loader for external 3pid sources
class MxSources
{
public:
    MxSources(MxStore& mxs);
    ~MxSources();
    bool init(VarCRef cfg, VarCRef env);

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
        };
        u64 purgeEvery;
        std::vector<InputEntry> list;
    };

private:
    std::vector<std::string> _argstrs;
    void _loop_th();
    void _loop_th_untilPurge();
    DataTree *_ingestData(const Config::InputEntry& entry) const; // must delete returned ptr
    void _ingestDataAndMerge(const Config::InputEntry& entry);
    std::future<DataTree*> _ingestDataAsync(const Config::InputEntry& entry);
    std::future<void> _ingestDataAndMergeAsync(const Config::InputEntry& entry);

    void _rebuildTree();
    void _updateEnv(VarCRef env);
    static void _Loop_th(MxSources *self);
    MxStore& _store; // this lives in MxStore
    Config _cfg;
    std::thread _th;
    std::atomic<bool> _quit;
    std::condition_variable _waiter;
    std::mutex _waitlock;
    std::vector<std::string> _envStrings;
    std::vector<const char*> _envPtrs;
};

