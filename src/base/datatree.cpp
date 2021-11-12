#include "datatree.h"
#include <assert.h>
#include "accessor.h"
#include "util.h"
#include "treeiter.h"
#include "pathiter.h"
#include "fetcher.h"


// Const-accessors -- safe to cast the const away since walking the tree doesn't change it

VarCRef DataTree::subtree(const char *path) const
{
    return VarCRef(const_cast<DataTree*>(this)->subtree(path));
}


DataTree::DataTree()
{
}

DataTree::~DataTree()
{
    _root.clear(*this);
}

VarRef DataTree::root()
{
    return VarRef(*this, &_root);
}

VarCRef DataTree::root() const
{
    return VarCRef(*this, &_root);
}

VarRef DataTree::subtree(const char* path, SubtreeQueryFlags flags)
{
    Var* p = &_root;
    if(!*path)
        return VarRef(*this, p);

    assert(*path == '/');
    Var::Extra* lastExtra = NULL;

    PathIter it(path);
    for(; p && it.hasNext(); ++it)
    {
        PoolStr ps = it.value();
        switch(p->type())
        {
            default:
                if(!(flags & SQ_CREATE))
                {
                    p = NULL;
                    break; // thing isn't container
                }
                // it's not a map, make it one
                p->makeMap(*this);
                [[fallthrough]];
            case Var::TYPE_MAP:
            {
                StrRef ref = this->lookup(ps.s, ps.len);
                Var *nextp = p->lookup(ref);
                if((flags & SQ_CREATE) && !nextp)
                    nextp = &p->map_unsafe()->putKey(*this, ps.s, ps.len);
                p = nextp;
                if (p)
                    lastExtra = p->getExtra();
                break;
            }
            break;

            case Var::TYPE_ARRAY:
            {
                // try looking up using an integer
                size_t idx;
                NumConvertResult cvt = strtosizeNN(&idx, ps.s, ps.len);
                // In case not all chars were consumed, it's an invalid key
                if(!cvt.used || cvt.overflow || cvt.used != ps.len)
                {
                    p = NULL;
                    break;
                }
                Var *nextp = p->at(idx);
                if((flags & SQ_CREATE) && !nextp)
                    if(size_t newsize = idx + 1) // overflow check
                        nextp = &p->makeArray(*this, newsize)[idx];
                p = nextp;
            }
            break;
        }
    }

    if (!p && !(flags & SQ_NOFETCH) && lastExtra)
    {
        Fetcher* f = lastExtra->fetcher;
        Var tmp;
        f->fetchAll(VarRef(this, &tmp), it.remain());
        tmp.clear(*this);
        // FIXME: make sure this works
    }

    return VarRef(*this, p);
}

struct CollectExpiredVars : public MutTreeIterFunctor
{
    CollectExpiredVars(u64 now, std::vector<Var*>& vec) : _now(now), _vec(vec) {}

    // Var was encountered. Return true to recurse (eventually End*() will be called).
    // Return false not to recurse (End*() will not be called)
    bool operator()(VarRef v) const
    {
        if (Var::Map* m = v.v->map())
        {
            if (m->isExpired(_now))
            {
                _vec.push_back(v.v);
                return false; // don't recurse into expired things
            }
        }
        return v.v->isContainer();
    }

    void EndArray(VarRef v) {}
    void EndObject(VarRef v) {}
    void Key(const char* k, size_t len) {} // encountered a map key (op() will be called next)

    const u64 _now;
    std::vector<Var*>& _vec;
};

void DataTree::fillExpiredSubnodes(std::vector<Var*>& v)
{
    CollectExpiredVars f(timeNowMS(), v);
    treeIter_T(f, root());
}
