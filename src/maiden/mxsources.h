#pragma once



#include "variant.h"
#include <thread>
#include <atomic>
#include <vector>
#include <condition_variable>
#include <mutex>
#include <future>

class MxStore;

// Periodic loader for external 3pid sources
class MxSources
{
public:
    MxSources(MxStore& mxs);
    ~MxSources();
    bool init(VarCRef cfg);

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
            std::string fn; // entry is valid if this is not empty
            u64 every;
        };
        u64 purgeEvery;
        std::vector<InputEntry> list;
    };

private:
    void _loop_th();
    void _loop_th_untilPurge();
    void _ingestData(const Config::InputEntry& entry);
    std::future<void> _ingestDataAsync(const Config::InputEntry& entry);
    void _rebuildTree();
    static void _Loop_th(MxSources *self);
    MxStore& _store;
    Config _cfg;
    std::thread _th;
    std::atomic<bool> _quit;
    std::condition_variable _waiter;
    std::mutex _waitlock;
};

