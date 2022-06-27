#include "mxsources.h"
#include "util.h"
#include "mxstore.h"
#include "json_in.h"
#include "subproc.h"
#include <assert.h>
#include "scopetimer.h"

MxSources::MxSources(MxStore& mxs)
    : _store(mxs)
{
}

MxSources::~MxSources()
{
    _quit = true;
    _waiter.notify_all();

    if(_th.joinable())
        _th.join();
}

static const char *addString(VarCRef x, std::vector<const char *>& ptrs, std::vector<std::string>& strtab)
{
    const char* p = x.asCString();
    if (p)
    {
        ptrs.push_back((const char*)(uintptr_t)strtab.size()); // rel index; to be fixed up later
        strtab.push_back(p);
    }
    return p;
}

static MxSources::Config::InputEntry parseInputEntry(VarCRef x, std::vector<std::string>& strtab)
{
    MxSources::Config::InputEntry entry;
    entry.every = 0;

    VarCRef xwhat = x.lookup("exec");
    if(xwhat)
    {
        entry.how = MxSources::Config::IN_EXEC;
        if(!addString(xwhat, entry.args, strtab))
            return entry;
    }
    else
    {
        xwhat = x.lookup("load");
        entry.how = MxSources::Config::IN_LOAD;
        switch(xwhat.type())
        {
            case Var::TYPE_STRING:
                addString(xwhat, entry.args, strtab);
                break;
            case Var::TYPE_ARRAY:
            {
                bool ok = false;
                if(size_t n = xwhat.size())
                {
                    ok = true;
                    for(size_t i = 0; i < n; ++i)
                        if(VarCRef xx = xwhat.at(i))
                        {
                            ok = !!addString(xx, entry.args, strtab);
                            if(!ok)
                                break;
                        }
                }
                if(!ok)
                {
                    entry.args.clear();
                    return entry;
                }
            }
            break;
        }
    }

    if(VarCRef xevery = x.lookup("every"))
        if(const char *p = xevery.asCString())
            if(!strToDurationMS_Safe(&entry.every, p))
            {
                printf("MxSources ERROR: every '%s' is not a valid duration", p);
                return entry; // still invalid here and will be skipped by caller
            }


    return entry;
}

bool MxSources::init(VarCRef src)
{
    VarCRef xlist = src.lookup("list");
    if(xlist && xlist.type() == Var::TYPE_ARRAY)
    {
        const size_t N = xlist.size();
        for(size_t i = 0; i < N; ++i)
        {
            Config::InputEntry e = parseInputEntry(xlist.at(i), _argstrs);
            if(!e.args.empty())
                _cfg.list.push_back(std::move(e));
        }
    }

    if(_cfg.list.empty())
    {
        printf("MxSources: Source list is empty\n");
        return false;
    }

    // fixup all pointers
    for(size_t i = 0; i < _cfg.list.size(); ++i)
    {
        auto& a = _cfg.list[i].args;
        for(size_t k = 0; k < a.size(); ++k)
            a[k] = _argstrs[(uintptr_t)a[k]].c_str();
        a.push_back(NULL); // terminator
    }

    _cfg.purgeEvery = 0;
    if(VarCRef xpurge = src.lookup("purgeEvery"))
        if(const char *p = xpurge.asCString())
            if(!strToDurationMS_Safe(&_cfg.purgeEvery, p))
            {
                printf("MxSources: Failed to parse 'purgeEvery' field\n");
                return false;
            }

    printf("MxSources: %u sources configured\n", (unsigned)_cfg.list.size());
    printf("MxSources: Purge tree every %ju seconds\n", _cfg.purgeEvery / 1000);

    printf("MxSources: Populating initial tree...\n");
    _rebuildTree();

    _th = std::thread(_Loop_th, this);
    printf("MxSources: ... done & spawned background thread\n");
    return true;
}

void MxSources::_loop_th_untilPurge()
{
    const size_t N = _cfg.list.size();
    u64 now = timeNowMS();

    std::vector<u64> whens(N);
    for(size_t i = 0; i < N; ++i)
        whens[i] = now + _cfg.list[i].every;

    u64 purgeWhen = _cfg.purgeEvery ? now + _cfg.purgeEvery : 0;

    std::vector<std::future<void> > futs;
    futs.reserve(N);

    while(!_quit && (!purgeWhen || now < purgeWhen))
    {
        for(size_t i = 0; i < futs.size(); ++i)
            futs[i].wait();
        futs.clear();

        std::unique_lock lock(_waitlock);

        now = timeNowMS();
        u64 mintime = u64(-1);
        for(size_t i = 0; i < N; ++i)
        {
            u64 every = _cfg.list[i].every;
            if(!every) // don't repeat once-only loads
                continue;

            u64 when = whens[i];
            if(now < when)
            {
                // not yet time... wait some more
                mintime = std::min(mintime, when - now);
            }
            else
            {
                futs.push_back(std::move(_ingestDataAndMergeAsync(_cfg.list[i])));
                whens[i] = now + every;
                mintime = std::min(mintime, every);
            }
        }

        printf("MxSources: Sleeping for up to %ju secs until next job\n", mintime / 1000);
        _waiter.wait_for(lock, std::chrono::milliseconds(mintime));
    }
}

DataTree *MxSources::_ingestData(const Config::InputEntry& entry) const
{
    assert(entry.args.size());
    DataTree *ret = new DataTree;

    ScopeTimer timer;

    bool ok = false;
    switch(entry.how)
    {
        case Config::IN_LOAD:
        if(const char *fn = entry.args[0])
            if(FILE *fh = fopen(fn, "rb"))
            {
                char buf[1024*4];
                BufferedFILEReadStream sm(fh, buf, sizeof(buf));
                ok = loadJsonDestructive(ret->root(), sm);
                fclose(fh);
            }
        break;

        case Config::IN_EXEC:
            ok = loadJsonFromProcess(ret->root(), &entry.args[0], NULL); // TODO: env
        break;
    }

    const u64 loadedMS = timer.ms();

    // for error reporting
    const char *str = entry.args[0];

    if(!ok)
        printf("MxSources: ERROR: Failed to ingest '%s'\n", str);
    else if(ret->root().type() == Var::TYPE_MAP)
        printf("MxSources: Ingested '%s' in %ju ms\n", str, loadedMS);
    else
    {
        printf("MxSources: WARNING: Ignored [%s], result type is not map\n", str);
        ok = false;
    }

    if(!ok)
    {
        delete ret;
        ret = NULL;
    }

    return ret;
}

void MxSources::_ingestDataAndMerge(const Config::InputEntry& entry)
{
    if(DataTree *tre = _ingestData(entry))
    {
        ScopeTimer timer;
        _store.merge3pid(tre->root());
        printf("MxSources: ... and merged '%s' in %ju ms\n", entry.args[0], timer.ms());
        delete tre;
    }
}

std::future<DataTree*> MxSources::_ingestDataAsync(const Config::InputEntry& entry)
{
    return std::async(std::launch::async, &MxSources::_ingestData, this, entry);
}

std::future<void> MxSources::_ingestDataAndMergeAsync(const Config::InputEntry& entry)
{
    return std::async(std::launch::async, &MxSources::_ingestDataAndMerge, this, entry);
}

void MxSources::_rebuildTree()
{
    ScopeTimer timer;

    // get subtrees in parallel
    const size_t N = _cfg.list.size();
    std::vector<std::future<DataTree*> > futs;
    futs.reserve(N);
    for(size_t i = 0; i < N; ++i)
        futs.push_back(std::move(_ingestDataAsync(_cfg.list[i])));
    for(size_t i = 0; i < N; ++i)
        futs[i].wait();

    const u64 loadedMS = timer.ms();
    printf("MxSources: Done loading %u subtrees after %ju ms\n", (unsigned)N, loadedMS);

    // swap in atomically
    {
        DataTree::LockedRoot lockroot = _store.get3pidRoot();
        // -------------------------------------
        lockroot.ref.clear();
        for (size_t i = 0; i < N; ++i)
        {
            if(DataTree *tre = futs[i].get())
            {
                _store.merge3pid_nolock(tre->root()); // we're already holding the lock
                delete tre;
            }
        }
    }

    const u64 mergedMS = timer.ms();

    printf("MxSources: Tree rebuilt, merged in %ju ms\n", mergedMS - loadedMS);
}


void MxSources::_loop_th()
{
    goto begin; // initial tree is constructed in init(), skip this here

    while(!_quit)
    {
        // construct initial tree
        _rebuildTree();

        begin:
        // do incremental updates
        _loop_th_untilPurge();
    }
}


void MxSources::_Loop_th(MxSources* self)
{
    self->_loop_th();
    printf("MxSources: Background thread exiting\n");
}
