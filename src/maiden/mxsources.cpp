#include "mxsources.h"
#include "util.h"
#include "mxstore.h"


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

static MxSources::Config::InputEntry parseInputEntry(VarCRef x)
{
    MxSources::Config::InputEntry entry;
    entry.every = 0;

    VarCRef xwhat = x.lookup("exec");
    if(xwhat)
        entry.how = MxSources::Config::IN_EXEC;
    else
    {
        xwhat = x.lookup("load");
        entry.how = MxSources::Config::IN_LOAD;
    }

    if(VarCRef xevery = x.lookup("every"))
        if(const char *p = xevery.asCString())
            if(!strToDurationMS_Safe(&entry.every, p))
            {
                printf("MxSources ERROR: every '%s' is not a valid duration", p);
                return entry; // still invalid here and will be skipped by caller
            }

    if(xwhat)
        if(const char *w = xwhat.asCString())
            entry.fn = w;

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
            Config::InputEntry e = parseInputEntry(xlist.at(i));
            if(!e.fn.empty())
                _cfg.list.push_back(std::move(e));
        }
    }

    if(_cfg.list.empty())
    {
        printf("MxSources: Source list is empty\n");
        return false;
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

    _th = std::thread(_Loop_th, this);
    printf("MxSources: Spawned background thread\n");
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

    while(!_quit && (!purgeWhen || now < purgeWhen))
    {
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
                // TODO do this async
                DataTree tmp;
                _ingestData(tmp.root(), _cfg.list[i]);
                if(tmp.root().type() == Var::TYPE_MAP)
                    _store.merge3pid(tmp.root());
                else
                    printf("MxSources: WARNING: Ignored [%s], result type is not map\n", _cfg.list[i].fn.c_str());

                whens[i] = now + every;
                mintime = std::min(mintime, every);

            }
        }

        printf("MxSources: Sleeping for up to %ju secs until next job\n", mintime / 1000);
        _waiter.wait_for(lock, std::chrono::milliseconds(mintime));

    }
}

void MxSources::_ingestData(VarRef where, const Config::InputEntry& entry)
{


    // TODO load data

}


void MxSources::_loop_th()
{
    while(!_quit)
    {
        // construct initial tree
        {
            DataTree::LockedRoot lockroot = _store.get3pidRoot();
            lockroot.ref.clear();
            const size_t N = _cfg.list.size();
            for(size_t i = 0; i < N; ++i)
            {
                // TODO: do all in parallel
                _ingestData(lockroot.ref, _cfg.list[i]);
            }
        }


        // do incremental updates
        _loop_th_untilPurge();
    }
}


void MxSources::_Loop_th(MxSources* self)
{
    self->_loop_th();
    printf("MxSources: Background thread exiting\n");
}
