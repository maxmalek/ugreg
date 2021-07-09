#include "datatree.h"
#include <assert.h>
#include "accessor.h"
#include "util.h"


// Const-accessors -- safe to cast the const away since walking the tree doesn't change it

const Var* DataTree::subtree(const Accessor& a) const
{
    return const_cast<DataTree*>(this)->subtree(a);
}

const Var* DataTree::subtree(const char *path) const
{
    return const_cast<DataTree*>(this)->subtree(path);
}


Var* DataTree::subtree(const Accessor& a)
{
    const size_t n = a.size();
    Var *p = &root;

    for(size_t i = 0; p && i < n; ++i)
    {
        const Var& k = a[i];
        switch(k.type())
        {
            case Var::TYPE_UINT: p = p->at(k.u.i); break;
            case Var::TYPE_STRING: p = p->lookup(k.u.s); break;
            default: assert(false); p = NULL; // can't happen: only int or string in accessors!
        }
    }

    return p;
}

Var* DataTree::subtree(const char* path)
{
    Var* p = &root;
    if(!*path)
        return p;

    if(*path == '#') // FIXME: correctly decode JSON pointers below, then this can go.
        return NULL; // unsupported for now

    if(*path != '/') // JSON pointers are defined to start with a '/'
        return NULL;


    // given "/" only, path is now "", which resolves correctly to an object with key "".

    do
    {
        assert(*path == '/');
        ++path; // skip the '/'

        switch(p->type())
        {
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
                        p = p->lookup(beg, len);
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
                    return NULL;
                p = p->at(idx);
            }
            break;

            default:
                return NULL; // thing isn't container
        }
    }
    while(p && *path);
    return p;
}
