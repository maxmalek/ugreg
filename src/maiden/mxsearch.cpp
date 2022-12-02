#include "mxsearch.h"
#include "mxstore.h"
#include "scopetimer.h"
#include "strmatch.h"
#include <string.h>

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

void MxSearch::rebuildCache(VarCRef src)
{
    std::unique_lock lock(mutex);       // R+W
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
        kc.ref = src.mem->lookup(it->first.c_str(), it->first.length());
        if(kc.ref)
        {
            kc.f = it->second;
            keys.push_back(kc);
        }
    }

    size_t N = keys.size();
    assert(N);

    const Var::Map *m = src.v->map();
    assert(m);

    clear();
    _strings.reserve(m->size());
    _keys.reserve(m->size());

    std::vector<unsigned char> tmp;

    size_t stringmem = 0;
    for (Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
        if(const Var::Map *user = it.value().map())
        {
            for(size_t i = 0; i < N; ++i)
                if(const Var *v =  user->get(keys[i].ref))
                {
                    PoolStr ps = v->asString(*src.mem);
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
                stringmem += ms.len;
                _strings.push_back(ms);
                _keys.push_back(it.key()); // this is the StrRef of the mxid
                tmp.clear();
            }
        }

    printf("MxSearch::rebuildCache() done after %u ms, using %zu KB for %zu strings\n",
        (unsigned)timer.ms(), stringmem/1024, _strings.size());
}

MxSearch::Matches MxSearch::search(const MxMatcherList& matchers, bool fuzzy, const TwoWayCasefoldMatcher *fullmatch) const
{
    std::shared_lock lock(mutex);
    //-----------------------------------------------------------

    Matches hits;
    ScopeTimer timer;
    const size_t N = _strings.size();
    for(size_t i = 0; i < N; ++i)
    {
        int score = mxMatchAndScore_Exact(_strings[i].s, _strings[i].len, matchers.data(), matchers.size());
        if(fuzzy)
            score += mxMatchAndScore_Fuzzy(_strings[i].s, matchers.data(), matchers.size());
        if(score > 0)
        {
            Match m;
            m.key = _keys[i];
            m.score = score;
            m.full = fullmatch && fullmatch->match(_strings[i].s, _strings[i].len);
            hits.push_back(m);
        }
    }
    printf("MxSearch::search() took %u ms\n", (unsigned)timer.ms());
    return hits;
}

void MxSearch::clear()
{
    for(size_t i = 0; i < _strings.size(); ++i)
        _stralloc.Free(_strings[i].s, _strings[i].len);
    _strings.clear();
    _keys.clear();
}

void MxSearch::onTreeRebuilt(VarCRef src)
{
    this->rebuildCache(src);
}
