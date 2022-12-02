#include "mxsources.h"
#include "util.h"
#include "mxstore.h"
#include "json_in.h"
#include "subproc.h"
#include <assert.h>
#include "scopetimer.h"
#include "env.h"
#include <algorithm>

MxSources::MxSources()
    : _quit(false)
{
    _merged.root().makeMap();
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
        switch (xwhat.type())
        {
            case Var::TYPE_STRING:
                addString(xwhat, entry.args, strtab);
                break;
            case Var::TYPE_ARRAY:
            {
                bool ok = false;
                if (size_t n = xwhat.size())
                {
                    ok = true;
                    for (size_t i = 0; i < n; ++i)
                        if (VarCRef xx = xwhat.at(i))
                        {
                            ok = !!addString(xx, entry.args, strtab);
                            if (!ok)
                                break;
                        }
                }
                if (!ok)
                {
                    entry.args.clear();
                    return entry;
                }
            }
            break;
        }
    }
    else
    {
        xwhat = x.lookup("load");
        entry.how = MxSources::Config::IN_LOAD;
        if(!addString(xwhat, entry.args, strtab))
            return entry;
    }

    if(VarCRef xevery = x.lookup("every"))
        if(const char *p = xevery.asCString())
            if(!strToDurationMS_Safe(&entry.every, p))
            {
                printf("MxSources ERROR: every '%s' is not a valid duration", p);
                return entry; // still invalid here and will be skipped by caller
            }

    // TODO: --check


    return entry;
}

bool MxSources::initConfig(VarCRef src, VarCRef env)
{
    _updateEnv(env);

    VarCRef xlist = src.lookup("list");
    if(xlist && xlist.type() == Var::TYPE_ARRAY)
    {
        const size_t N = xlist.size();
        for(size_t i = 0; i < N; ++i)
        {
            Config::InputEntry e = parseInputEntry(xlist.at(i), _argstrs);
            if(!e.args.empty())
                _cfg.list.push_back(std::move(e));
            else
                printf("MxSources: Failed to parse entry[%u] in sources list\n", unsigned(N));
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

    VarCRef xdirectory = src.lookup("directory");
    if (xdirectory && xdirectory.type() == Var::TYPE_STRING)
    {
        _cfg.directory = xdirectory.asCString();
        if (!_cfg.directory.empty() && _cfg.directory.back() != '/')
            _cfg.directory += '/';
    }

    if (_cfg.directory.empty())
        printf("MxSources: Not touching the disk. RAM only.\n");
    else
        printf("MxSources: Cache directory = %s\n", _cfg.directory.c_str());

    printf("MxSources: %u sources configured\n", (unsigned)_cfg.list.size());
    printf("MxSources: Purge tree every %ju seconds\n", _cfg.purgeEvery / 1000);
    return true;
}

void MxSources::initPopulate()
{
    assert(!_th.joinable());

    printf("MxSources: Populating initial tree...\n");
    _rebuildTree();

    _th = std::thread(_Loop_th, this);
    printf("MxSources: ... done & spawned background thread\n");
}

void MxSources::_loop_th_untilPurge()
{
    const size_t N = _cfg.list.size();
    u64 now = timeNowMS();

    std::vector<u64> whens(N);
    for(size_t i = 0; i < N; ++i)
        whens[i] = now + _cfg.list[i].every;

    const u64 purgeWhen = _cfg.purgeEvery ? now + _cfg.purgeEvery : 0;

    std::vector<std::future<DataTree*> > futs; // always NULL values
    futs.reserve(N);

    while(!_quit && (!purgeWhen || now < purgeWhen))
    {
        // finish up any remaining background jobs
        for(size_t i = 0; i < futs.size(); ++i)
            futs[i].wait();
        futs.clear();

        // sync time; exit when it's time to purge
        now = timeNowMS();
        u64 mintime = u64(-1);

        u64 timeUntilPurge = 0;
        if(now < purgeWhen)
        {
            timeUntilPurge = purgeWhen - now;
            mintime = std::min(mintime, timeUntilPurge);
        }
        else if(purgeWhen)
            break;

        std::unique_lock lock(_waitlock); // this is just to make _waiter below work

        // spawn async jobs that will auto-merge themselves when done
        for(size_t i = 0; i < N; ++i)
        {
            const u64 every = _cfg.list[i].every;
            if(!every) // don't repeat once-only loads
                continue;

            const u64 when = whens[i];
            if(now < when)
            {
                // not yet time... wait some more
                mintime = std::min(mintime, when - now);
            }
            else
            {
                // merge directly into existing tree
                futs.push_back(std::move(_ingestDataAndMergeAsync(&_merged, _cfg.list[i])));
                whens[i] = now + every;
                mintime = std::min(mintime, every);
            }
        }

        if(mintime)
        {
            printf("MxSources: Sleeping for up to %ju ms until next job (%ju ms until purge)\n",
                mintime, timeUntilPurge);
            // this wait can get interrupted to exit early
            _waiter.wait_for(lock, std::chrono::milliseconds(mintime));
        }
    }
}

DataTree *MxSources::_ingestData(const Config::InputEntry& entry) const
{
    assert(entry.args.size());

    // for error reporting
    const char *str = entry.args[0];

    printf("MxSources: * Starting ingest '%s' ...\n", str);
    DataTree *ret = new DataTree;
    ScopeTimer timer;
    bool ok = false;

    switch(entry.how)
    {
        case Config::IN_LOAD:
            ok = serialize::load(ret->root(), entry.args[0]);
        break;

        case Config::IN_EXEC:
            ok = loadJsonFromProcess(ret->root(), &entry.args[0], _envPtrs.data()); // TODO: env
        break;
    }

    const u64 loadedMS = timer.ms();

    if(!ok)
        printf("MxSources: * ERROR: Failed to ingest '%s'\n", str);
    else if(ret->root().type() == Var::TYPE_MAP)
        printf("MxSources: * ... Ingested '%s' in %ju ms\n", str, loadedMS);
    else
    {
        printf("MxSources: * WARNING: Ignored [%s], result type is not map\n", str);
        ok = false;
    }

    if(!ok)
    {
        delete ret;
        ret = NULL;
    }

    return ret;
}

DataTree *MxSources::_ingestDataAndMerge(DataTree *dst, const Config::InputEntry& entry)
{
    if(DataTree *tre = _ingestData(entry))
    {
        if(VarCRef data = tre->root().lookup("data"))
        {
            if(data.type() == Var::TYPE_MAP)
            {
                if(dst)
                {
                    u64 ms;
                    {
                        DataTree::LockedRef locked = dst->lockedRef();
                        //----------------------------------
                        ScopeTimer timer;
                        locked.ref.merge(data, MERGE_RECURSIVE);
                        ms = timer.ms();
                    }
                    printf("MxSources: * ... and merged '%s' in %ju ms\n", entry.args[0], ms);
                    // preceed to delete it
                }
                else
                    return tre;
            }
            else
                printf("MxSources: ERROR: Ingest '%s': value under key 'data' is not map, ignoring\n", entry.args[0]);
        }
        else
            printf("MxSources: WARNING: Ingest '%s' has no 'data' key, skipping\n", entry.args[0]);
    
        delete tre;
    }

    return NULL;
}

std::future<DataTree*> MxSources::_ingestDataAndMergeAsync(DataTree *dst, const Config::InputEntry& entry)
{
    return std::async(std::launch::async, &MxSources::_ingestDataAndMerge, this, dst, entry);
}

void MxSources::_rebuildTree()
{
    ScopeTimer timer;
    
    DataTree newtree;

    // get subtrees in parallel
    // DO NOT lock anything just yet -- the ingest might take quite some time!
    // While we're still here and ingesting, continue serving the old tree
    const size_t N = _cfg.list.size();
    {
        std::vector<std::future<DataTree*> > futs;
        futs.reserve(N);
        for(size_t i = 0; i < N; ++i)
            futs.push_back(std::move(_ingestDataAndMergeAsync(&newtree, _cfg.list[i])));
    }
    // --- Futures are done now ---

    const u64 loadedMS = timer.ms();
    printf("MxSources: Done loading %u subtrees after %ju ms\n", (unsigned)N, loadedMS);

    // swap in as atomically as possible
    {
        DataTree::LockedRef locked = this->lockedRef();
        // -------------------------------------
        Var del(std::move(*locked.ref.v)); // keep this until we're done merging -- it's likely we can re-use a lot of strings in the pool
        locked.ref.merge(newtree.root(), MERGE_FLAT); // the old tree is gone, so just copy things over
        del.clear(*locked.ref.mem); // old things can go now
        locked.ref.mem->defrag();
    }
    printf("MxSources: Tree rebuilt, merged in %ju ms\n", timer.ms() - loadedMS);

    _sendTreeRebuiltEvent();
}

static void _OnTreeRebuilt(EvTreeRebuilt *ev, VarCRef src)
{
    ev->onTreeRebuilt(src);
}

void MxSources::_sendTreeRebuiltEvent() const
{
    DataTree::LockedCRef locked = this->lockedCRef();
    //----------------------------------
    {
        std::vector<std::future<void> > futs;
        {
            std::unique_lock elock(_eventlock);
            //----------------------------------
            futs.resize(_evRebuilt.size());
            for(size_t i = 0; i < _evRebuilt.size(); ++i)
                futs[i] = std::move(std::async(_OnTreeRebuilt, _evRebuilt[i], locked.ref));
        }
        // don't keep events locked while the futures finish
    }
    // ... but keep the tree read-locked until all futures are done
}

void MxSources::_updateEnv(VarCRef xenv)
{
    _envStrings = enumerateEnvVars();
    _envPtrs.clear();

    const Var::Map* m = xenv.v->map();
    if (m)
    {
        std::string tmp;
        for (Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
        {
            const char* k = xenv.mem->getS(it.key());
            const char* v = it.value().asCString(*xenv.mem);
            if (k && v)
            {
                tmp = k;
                tmp += '=';
                tmp += v;
#ifdef _DEBUG
                printf("ENV: %s\n", tmp.c_str());
#else
                printf("ENV: %s\n", k);
#endif
                _envStrings.push_back(std::move(tmp));
            }
        }
    }

    const size_t n = _envStrings.size();
    _envPtrs.reserve(n + 1);
    for (size_t i = 0; i < n; ++i)
        _envPtrs.push_back(_envStrings[i].c_str());
    _envPtrs.push_back(NULL); // terminator
}


void MxSources::addListener(EvTreeRebuilt* ev)
{
    std::unique_lock elock(_eventlock);
    //----------------------------------
    for(size_t i = 0; i < _evRebuilt.size(); ++i)
        if(_evRebuilt[i] == ev)
            return;
    _evRebuilt.push_back(ev);
}

void MxSources::removeListener(EvTreeRebuilt* ev)
{
    std::unique_lock elock(_eventlock);
    //----------------------------------
    _evRebuilt.erase(std::remove(_evRebuilt.begin(), _evRebuilt.end(), ev));
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

MxSources::SearchResults MxSources::formatMatches(const MxSearchConfig& scfg, const MxSearch::Match* matches, size_t n, const char* term) const
{
    ScopeTimer timer;
    std::vector<SearchResult> res;
    res.reserve(n);

    {
        DataTree::LockedCRef locked = this->lockedCRef();
        //---------------------------------------

        const StrRef displaynameRef = locked.ref.mem->lookup(scfg.displaynameField.c_str(), scfg.displaynameField.length());
        const Var::Map* const m = locked.ref.v->map();

        for (size_t i = 0; i < n; ++i)
        {
            const StrRef key = matches[i].key;
            if (const Var* user = m->get(key))
                if (const Var::Map* um = user->map())
                {
                    PoolStr mxid = locked.ref.mem->getSL(key);
                    assert(mxid.s); // we just got the map key. this must exist.

                    SearchResult sr;
                    sr.str.assign(mxid.s, mxid.len);

                    if (const Var* xdn = um->get(displaynameRef))
                        if (const char* dn = xdn->asCString(*locked.ref.mem))
                            sr.displayname = dn;

                    if (scfg.element_hack && !matches[i].full)
                        sr.displayname = sr.displayname + "  // " + term;

                    res.push_back(std::move(sr));
                }
        }
    }

    printf("MxSources::formatMatches(): %zu/%zu results in %u ms\n",
        res.size(), n, unsigned(timer.ms()));

    return res;
}


static bool _lockAndSave(const DataTree::LockedCRef& locked, std::string fn)
{
    return serialize::save(fn.c_str(), locked.ref, serialize::ZSTD, serialize::BJ);
}

static bool _lockAndLoad(const DataTree::LockedRef& locked, std::string fn)
{
    return serialize::load(locked.ref, fn.c_str(), serialize::ZSTD, serialize::BJ);
}

bool MxSources::save() const
{
    if (_cfg.directory.empty())
        return false;

    printf("MxSources::save() starting...\n");
    ScopeTimer timer;
    bool ok = _lockAndSave(_merged.lockedCRef(), _cfg.directory + "mxsources.mxs");
    printf("MxSources::save() done in %u ms, success = %d\n", (unsigned)timer.ms(), ok);
    return ok;
}

bool MxSources::load()
{
    if (_cfg.directory.empty())
        return false;

    printf("MxSources::load() starting...\n");
    ScopeTimer timer;
    bool ok = _lockAndLoad(_merged.lockedRef(), _cfg.directory + "mxsources.mxs");
    printf("MxSources::load() done in %u ms, success = %d\n", (unsigned)timer.ms(), ok);

    if(ok)
        _sendTreeRebuiltEvent();

    return ok;
}
