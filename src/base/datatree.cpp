#include "datatree.h"
#include <assert.h>
#include "accessor.h"
#include "util.h"
#include "treeiter.h"
#include "pathiter.h"
#include "fetcher.h"


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

// TODO: check expiry
// TODO: check permissions
VarRef DataTree::subtree(const char* path, Var::SubtreeQueryFlags qf)
{
    LockableMem mr { *this, mutex };
    return VarRef(*this, _root.subtreeOrFetch(mr, path, qf));
}

VarCRef DataTree::subtreeConst(const char *path) const
{
    return VarCRef(*this, _root.subtreeConst(*this, path));
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
