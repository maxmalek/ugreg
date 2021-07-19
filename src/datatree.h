#pragma once

#include "variant.h"
#include "treemem.h"

class Accessor;

// Root of tree with memory pool
class DataTree : public TreeMem
{
    DataTree(const DataTree&) = delete;
    DataTree& operator=(const DataTree&) = delete;

public:
    DataTree();
    ~DataTree();

    VarRef  root();
    VarCRef root() const;

    // Use type-safe accessor.
    // Note that the accessor must be constructed using the same backing memory as this tree,
    // else it won't work when looking up string keys.
    // An empty accessor will return root().
    // Don't forget to check if the returned ref is valid before accessing it.
    VarRef  subtree(const Accessor& a);
    VarCRef subtree(const Accessor& a) const;

    // Use stringly typed JSON pointer (returns an invalid ref for malformed json pointers)
    // See https://rapidjson.org/md_doc_pointer.html#JsonPointer
    // and https://datatracker.ietf.org/doc/html/rfc6901
    // NULL is not allowed.
    // An empty string ("") returns the root, a valid json pointer ("/...") returns that,
    // and an invalid JSON pointer (one that starts not with '/') returns an invalid ref.
    // Don't forget to check if the returned ref is valid before accessing it.
    VarRef  subtree(const char *path);
    VarCRef subtree(const char* path) const;

    Var _root;
};
