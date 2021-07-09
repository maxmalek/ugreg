#pragma once

#include "variant.h"

class Accessor;


// TODO, once Var memory optimizations are in:
// this should be a construct of memory storage, allocator, and then the Var tree itself.
// for memory, we can even do things like string interning and so on.
class DataTree
{
public:
    // TODO: tree managemeent goes in here


    // Use type-safe accessor
          Var *subtree(const Accessor& a);
    const Var *subtree(const Accessor& a) const;

    // Use stringly typed JSON pointer (returns NULL for malformed json pointers)
    // See https://rapidjson.org/md_doc_pointer.html#JsonPointer
    // and https://datatracker.ietf.org/doc/html/rfc6901
          Var *subtree(const char *path);
    const Var *subtree(const char* path) const;


    Var root;
};
