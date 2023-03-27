#include "mxsources.h"
#include "util.h"
#include "mxstore.h"
#include "json_in.h"
#include "subproc.h"
#include <assert.h>
#include "scopetimer.h"
#include "env.h"
#include <algorithm>
#include <unordered_set>
#include "subprocess.h"

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
    entry.check = false;

    VarCRef xwhat = x.lookup("exec");
    if(xwhat)
    {
        entry.how = MxSources::Config::IN_EXEC;
        entry.check = true;
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

        if(VarCRef xcheck = x.lookup("check"))
            entry.check = xcheck.asBool();
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
                logerror("MxSources ERROR: every '%s' is not a valid duration", p);
                return entry; // still invalid here and will be skipped by caller
            }

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
                logerror("MxSources: Failed to parse entry[%u] in sources list", unsigned(N));
        }
    }

    if(_cfg.list.empty())
    {
        logerror("MxSources: Source list is empty");
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
                logerror("MxSources: Failed to parse 'purgeEvery' field");
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
        logdebug("MxSources: Not touching the disk. RAM only.");
    else
        logdebug("MxSources: Cache directory = %s", _cfg.directory.c_str());

    logdebug("MxSources: %u sources configured", (unsigned)_cfg.list.size());
    logdebug("MxSources: Purge tree every %ju seconds", _cfg.purgeEvery / 1000);

    return _checkAll();
}

bool MxSources::_checkAll() const
{
    size_t N = _cfg.list.size();
    for(size_t i = 0; i < N; ++i)
    {
        const Config::InputEntry& e = _cfg.list[i];
        if(e.how == Config::IN_EXEC && e.check)
        {
            if(!_checkExec(e))
                return false;
        }
    }
    return true;
}

bool MxSources::_checkExec(const Config::InputEntry& e) const
{
    subprocess_s proc;
    const char *args[] = { e.args[0], "--check", NULL };

    log("MxSources: Startup-check: \"%s\" --check ...", args[0]);

    // start in sync mode; async isn't needed here
    if (!createProcess(&proc, args, _envPtrs.data(), subprocess_option_combined_stdout_stderr | subprocess_option_no_window))
    {
        logerror("MxSources: Startup-check: Failed to create subprocess: \"%s\" --check", args[0]);
        return false;
    }

    // display subproc stdout+stderr
    {
        FILE* pout = subprocess_stdout(&proc);
        size_t bytes;
        char buf[256];
        do
        {
            bytes = fread(buf, 1, sizeof(buf), pout);
            fwrite(buf, 1, bytes, stdout);
        }
        while (bytes);
    }


    int ret = 0;
    int err = subprocess_join(&proc, &ret);
    subprocess_destroy(&proc);

    if (err)
    {
        logerror("MxSources: Startup-check: Failed subprocess_join()");
        return false;
    }

    if (ret)
    {
        logerror("MxSources: Startup-check: Failed with return code %d", ret);
        return false;
    }

    log("... OK");
    return true;
}

void MxSources::initPopulate(bool buildAsync)
{
    assert(!_th.joinable());

    if(!buildAsync)
    {
        logdebug("MxSources: Populating initial tree...");
        _rebuildTree();
        logdebug("MxSources: ... done\n");
    }

    _th = std::thread(_Loop_th, this, buildAsync);
    logdebug("MxSources: Spawned background thread");
}

void MxSources::_loop_th_untilPurge()
{
    const size_t N = _cfg.list.size();
    u64 now = timeNowMS();

    std::vector<u64> whens(N);
    for(size_t i = 0; i < N; ++i)
        whens[i] = now + _cfg.list[i].every;

    const u64 purgeWhen = _cfg.purgeEvery ? now + _cfg.purgeEvery : 0;

    std::vector<std::future<IngestResult> > futs; // always NULL values
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
            logdebug("MxSources: Sleeping for up to %ju ms until next job (%ju ms until purge)",
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

    log("MxSources: * Starting ingest '%s' ...", str);
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
        logerror("MxSources: * ERROR: Failed to ingest '%s'", str);
    else if(ret->root().type() == Var::TYPE_MAP)
        log("MxSources: * ... Ingested '%s' in %ju ms", str, loadedMS);
    else
        logerror("MxSources: * WARNING: Ingest '%s': Result type is not map", str);

    if(!ok)
    {
        delete ret;
        ret = NULL;
    }

    return ret;
}

MxSources::IngestResult MxSources::_ingestDataAndMerge(DataTree *dst, const Config::InputEntry& entry)
{
    IngestResult res;
    if(DataTree *tre = _ingestData(entry))
    {
        res.loaded = true;
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
                    logdebug("MxSources: * ... and merged '%s' in %ju ms", entry.args[0], ms);
                    res.merged = true;
                    // proceed to delete it
                }
                else
                    res.tree = tre;
            }
            else
            {
                logerror("MxSources: ERROR: Ingest '%s': value under key 'data' is not map, ignoring", entry.args[0]);
                res.ignored = true;
            }
        }
        else
        {
            logerror("MxSources: WARNING: Ingest '%s' has no 'data' key, skipping", entry.args[0]);
            res.ignored = true;
        }
        delete tre;
    }

    return res;
}

std::future<MxSources::IngestResult> MxSources::_ingestDataAndMergeAsync(DataTree *dst, const Config::InputEntry& entry)
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
        std::vector<std::future<IngestResult> > futs;
        futs.reserve(N);
        for(size_t i = 0; i < N; ++i)
            futs.push_back(std::move(_ingestDataAndMergeAsync(&newtree, _cfg.list[i])));
        unsigned fail = 0;
        for (size_t i = 0; i < N; ++i)
        {
            IngestResult res = futs[i].get();
            assert(!res.tree);
            fail += !res.loaded;
        }
        if(fail)
        {
            logerror("%u/%u ingests failed, aborting. Tree will remain unchanged.", fail, (unsigned)N);
            return;
        }
    }
    // --- Futures are done now ---

    const u64 loadedMS = timer.ms();
    logdebug("MxSources: Done loading %u subtrees after %ju ms", (unsigned)N, loadedMS);

    // swap in as atomically as possible
    {
        DataTree::LockedRef locked = this->lockedRef();
        // -------------------------------------
        Var del(std::move(*locked.ref.v)); // keep this until we're done merging -- it's likely we can re-use a lot of strings in the pool
        locked.ref.merge(newtree.root(), MERGE_FLAT); // the old tree is gone, so just copy things over
        del.clear(*locked.ref.mem); // old things can go now
        locked.ref.mem->defrag();
    }
    logdebug("MxSources: Tree rebuilt, merged in %ju ms", timer.ms() - loadedMS);

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
                futs[i] = std::move(std::async(std::launch::async, _OnTreeRebuilt, _evRebuilt[i], locked.ref));
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
                logdebug("ENV: %s", tmp.c_str());
#else
                logdebug("ENV: %s", k);
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

void MxSources::_loop_th(bool buildAsync)
{
    if(!buildAsync) // if buildAsync==false, _rebuilTree() was already called in initPopulate()
        goto skip;

    while(!_quit)
    {
        // construct initial tree
        _rebuildTree();

        skip:
        // do incremental updates
        _loop_th_untilPurge();
    }
}

void MxSources::_Loop_th(MxSources* self, bool buildAsync)
{
    self->_loop_th(buildAsync);
    logdebug("MxSources: Background thread exiting");
}

static void formatOneMatch(MxSearchResults& res, const TreeMem& mem, const StrRef displaynameRef, const Var::Map* const m, StrRef key)
{
    if (const Var* user = m->get(key))
        if (const Var::Map* um = user->map())
        {
            PoolStr mxid = mem.getSL(key);
            assert(mxid.s); // we just got the map key. this must exist.

            MxSearchResult sr;
            sr.mxid.assign(mxid.s, mxid.len);

            if (const Var* xdn = um->get(displaynameRef))
                if (const char* dn = xdn->asCString(mem))
                    sr.displayname = dn;

            res.push_back(std::move(sr));
        }
}

MxSearchResults MxSources::formatMatches(const MxSearchConfig& scfg, const MxSearch::Match* matches, size_t n,
    const MxSearchResults& hsresults, size_t limit) const
{
    ScopeTimer timer;
    MxSearchResults res;
    res.reserve(limit ? limit : (n + hsresults.size()));

    std::unordered_set<StrRef> hsrefLUT;
    std::vector<StrRef> hsrefVec;
    hsrefVec.reserve(hsresults.size());

    {
        DataTree::LockedCRef locked = this->lockedCRef();
        //---------------------------------------

        // First, take the existing entries in hsresults and translate them to
        // a stringpool ID. if that succeeds, there's likely a duplicate.
        for(size_t i = 0; i < hsresults.size(); ++i)
        {
            const MxSearchResult& sr = hsresults[i];
            const StrRef ref = locked.ref.mem->lookup(sr.mxid.c_str(), sr.mxid.length());
            if(ref)
            {
                hsrefLUT.insert(ref);
                hsrefVec.push_back(ref);
            }
        }
        DEBUG_LOG("MxSources::formatMatches() hsrefLUT size = %zu", hsrefLUT.size());

        // The field name is always the same ID so we can get that early to speed things up
        const StrRef displaynameRef = locked.ref.mem->lookup(scfg.displaynameField.c_str(), scfg.displaynameField.length());
        
        // This is the global search map: { mxid => { "displayname" = "...", ... } }
        const Var::Map* const m = locked.ref.v->map();

        // Do the users from hsresults first, so they end up first in the list.
        // This entures that later when HS's and our results are merged, duplicates
        // appear at the start, so that merging them works
        for(size_t i = 0; i < hsrefVec.size(); ++i) // intentionally not limiting here!
            formatOneMatch(res, *locked.ref.mem, displaynameRef, m, hsrefVec[i]);

        for (size_t i = 0; i < n && res.size() < limit; ++i)
        {
            const StrRef key = matches[i].key;
            if(hsrefLUT.find(key) == hsrefLUT.end())
                formatOneMatch(res, *locked.ref.mem, displaynameRef, m, key);
        }
    }

    logdebug("MxSources::formatMatches(): %zu/%zu results in %u ms",
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

    logdebug("MxSources::save() starting...");
    ScopeTimer timer;
    bool ok = _lockAndSave(_merged.lockedCRef(), _cfg.directory + "mxsources.mxs");
    logdebug("MxSources::save() done in %u ms, success = %d", (unsigned)timer.ms(), ok);
    return ok;
}

bool MxSources::load()
{
    if (_cfg.directory.empty())
        return false;

    logdebug("MxSources::load() starting...");
    ScopeTimer timer;
    bool ok = _lockAndLoad(_merged.lockedRef(), _cfg.directory + "mxsources.mxs");
    logdebug("MxSources::load() done in %u ms, success = %d", (unsigned)timer.ms(), ok);

    if(ok)
        _sendTreeRebuiltEvent();

    return ok;
}
