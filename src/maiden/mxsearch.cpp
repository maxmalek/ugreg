#include "mxsearch.h"
#include "mxstore.h"
#include "scopetimer.h"
#include "strmatch.h"

MxSearch::MxSearch(const MxSearchConfig& scfg)
    : scfg(scfg)
{
}

MxSearch::~MxSearch()
{
    clear();
}

bool MxSearch::init(VarCRef cfg)
{
    return true;
}

void MxSearch::rebuildCache(const MxStore & mxs)
{
    std::unique_lock lock(mutex);       // R+W
    DataTree::LockedCRef cr = mxs.get3pidCRoot();  // R only
    //-----------------------------------------------------------

    ScopeTimer timer;

    struct SearchKeyCache
    {
        StrRef ref;
        MxSearchConfig::Field f;
    };
    std::vector<SearchKeyCache> keys;

    // cache the keys so we don't need to do string->StrRef lookups all the time
    for (MxSearchConfig::Fields::const_iterator it = scfg.fields.begin(); it != scfg.fields.end(); ++it)
    {
        SearchKeyCache kc;
        kc.ref = cr.ref.mem->lookup(it->first.c_str(), it->first.length());
        if(kc.ref)
        {
            kc.f = it->second;
            keys.push_back(kc);
        }
    }

    size_t N = keys.size();
    assert(N);

    VarCRef dataroot = cr.ref.lookup("_data"); // FIXME: make this a cleaner accessor and not all over the place
    const Var::Map *m = dataroot ? dataroot.v->map() : NULL;
    assert(m);

    clear();
    _strings.reserve(m->size());
    _keys.reserve(m->size());

    std::vector<unsigned char> tmp;

    for (Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
        if(const Var::Map *user = it.value().map())
        {
            for(size_t i = 0; i < N; ++i)
                if(const Var *v =  user->get(keys[i].ref))
                {
                    PoolStr ps = v->asString(*cr.ref.mem);
                    if(ps.len)
                        if(!mxSearchNormalizeAppend(tmp, ps.s, ps.len))
                            printf("MxSearch: Got invalid UTF-8: %s\n", ps.s);
                }
            if(!tmp.empty())
            {
                MutStr ms;
                ms.len = tmp.size();
                ms.s = (char*)_stralloc.Alloc(ms.len);
                memcpy(ms.s, tmp.data(), ms.len);
                _strings.push_back(ms);
                _keys.push_back(it.key()); // this is the StrRef of the mxid
                tmp.clear();
            }
        }

    printf("MxSearch::rebuildCache() done after %u ms\n", (unsigned)timer.ms());
}

MxSearch::Matches MxSearch::searchExact(const TwoWayMatcher& matcher) const
{
    std::shared_lock lock(mutex);
    //-----------------------------------------------------------

    Matches hits;
    ScopeTimer timer;
    const size_t N = _strings.size();
    for(size_t i = 0; i < N; ++i)
    {
        // like strstr() but faster and doesn't stop on \0
        const char *s = matcher.match(_strings[i].s, _strings[i].len);
        if(s)
        {
            Match m;
            m.key = _keys[i];
            m.score = 10000; // TODO: make configurable?
            hits.push_back(m);
        }
    }
    printf("MxSearch::searchExact() took %u ms\n", (unsigned)timer.ms());
    return hits;
}

void MxSearch::clear()
{
    for(size_t i = 0; i < _strings.size(); ++i)
        _stralloc.Free(_strings[i].s, _strings[i].len);
    _strings.clear();
    _keys.clear();
}

void MxSearch::onTreeRebuilt(const MxStore& mxs)
{
    this->rebuildCache(mxs);
}
