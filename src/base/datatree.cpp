#include "datatree.h"
#include <assert.h>
#include "accessor.h"
#include "util.h"
#include "treeiter.h"


// Const-accessors -- safe to cast the const away since walking the tree doesn't change it

VarCRef DataTree::subtree(const Accessor& a) const
{
    return VarCRef(const_cast<DataTree*>(this)->subtree(a));
}

VarCRef DataTree::subtree(const char *path) const
{
    return VarCRef(const_cast<DataTree*>(this)->subtree(path, false));
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

VarRef DataTree::subtree(const Accessor& acc, bool create)
{
    const size_t n = acc.size();
    Var *p = &_root;

    for(size_t i = 0; p && i < n; ++i)
    {
        Var * const lastp = p;
        const Var& k = acc[i];
        const Var::Type kt = k.type();
        switch(kt)
        {
            case Var::TYPE_UINT: p = p->at(k.u.ui); break;
            case Var::TYPE_STRING: p = p->lookup(k.u.s); break;
            default: assert(false); p = NULL; // can't happen: only int or string in accessors!
        }

        if(create && lastp)
        {
            switch(kt)
            {
                case Var::TYPE_UINT:
                    if(size_t arraysize = k.u.ui + 1) // make sure this won't be an issue if this overflows
                        p = &lastp->makeArray(*this, k.u.ui + 1)[k.u.ui]; // either it wasn't an array, or not large enough
                    break;
                case Var::TYPE_STRING:
                    p = &lastp->makeMap(*this)->put(*this, k.u.s, std::move(Var()));
                    break;
                default: assert(false); p = NULL; // can't happen: only int or string in accessors!
            }
        }
    }

    return VarRef(*this, p);
}

VarRef DataTree::subtree(const char* path, bool create)
{
    Var* p = &_root;
    if(!*path)
        return VarRef(*this, p);

    if(*path == '#') // FIXME: correctly decode JSON pointers below, then this can go.
        return VarRef(*this, NULL); // unsupported for now

    if(*path != '/') // JSON pointers are defined to start with a '/'
        return VarRef(*this, NULL);


    // given "/" only, path is now "", which resolves correctly to an object with key "".

    do
    {
        assert(*path == '/');
        ++path; // skip the '/'
        Var * const lastp = p;
        switch(p->type())
        {
            default:
                if(!create)
                {
                    p = NULL;
                    break; // thing isn't container
                }
                // it's not a map, make it one
                p->makeMap(*this);
                [[fallthrough]];
            case Var::TYPE_MAP:
            {
                // FIXME: correctly decode JSON pointers here (escapes and #)
                // see https://rapidjson.org/md_doc_pointer.html#JsonPointer
                // and https://datatracker.ietf.org/doc/html/rfc6901
                const char* beg = path;
                for(;; path++)
                {
                    char c = *path;
                    if(!c || c == '/')
                    {
                        const size_t len = path - beg;
                        StrRef ref = this->lookup(beg, len);
                        Var *nextp = p->lookup(ref);
                        if(create && !nextp)
                            nextp = &p->map_unsafe()->putKey(*this, beg, len);
                        p = nextp;
                        break;
                    }
                }
            }
            break;

            case Var::TYPE_ARRAY:
            {
                // try looking up using an integer
                size_t idx;
                NumConvertResult cvt = strtosizeNN(&idx, path, -1); // consume as many as possible
                path += cvt.used;
                if(!cvt.used || cvt.overflow)
                    return VarRef(*this, NULL);
                Var *nextp = p->at(idx);
                if(create && !nextp)
                    if(size_t newsize = idx + 1) // overflow check
                        nextp = &p->makeArray(*this, newsize)[idx];
                p = nextp;
            }
            break;
        }
    }
    while(p && *path);

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
