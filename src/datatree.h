#pragma once

#include "variant.h"
#include "treemem.h"

class Accessor;

// Root of tree with memory pool
class DataTree : public TreeMem
{
public:
    // TODO: tree managemeent goes in here
    ~DataTree();

    VarRef  root();
    VarCRef root() const;

    // Use type-safe accessor
    VarRef  subtree(const Accessor& a);
    VarCRef subtree(const Accessor& a) const;

    // Use stringly typed JSON pointer (returns NULL for malformed json pointers)
    // See https://rapidjson.org/md_doc_pointer.html#JsonPointer
    // and https://datatracker.ietf.org/doc/html/rfc6901
    VarRef  subtree(const char *path);
    VarCRef subtree(const char* path) const;


    Var _root;
};
