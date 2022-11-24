#pragma once

#include "variant.h"
#include "treemem.h"

#include "upgrade_mutex.h"
#include <vector>

// Root of tree with memory pool and mutex
class DataTree : public TreeMem
{
    DataTree(const DataTree&) = delete;
    DataTree& operator=(const DataTree&) = delete;

public:
    DataTree(StringPool::PoolSize size = StringPool::DEFAULT);
    ~DataTree();

    VarRef  root();
    VarCRef root() const;

    // Used to keep the tree locked while it's accessed. Intended for writing.
    struct LockedRoot
    {
        friend class DataTree;
        VarRef ref;
    private:
        std::unique_lock<acme::upgrade_mutex> _lock; // lock R+W
        LockedRoot(DataTree& tree)
            :_lock(tree.mutex), ref(tree.root()) {}
    };

    inline LockedRoot lockedRoot() { return LockedRoot(*this); }

    struct LockedCRoot
    {
        friend class DataTree;
        VarCRef ref;
    private:
        std::shared_lock<acme::upgrade_mutex> _lock; // lock R
        LockedCRoot(const DataTree& tree)
            :_lock(tree.mutex), ref(tree.root()) {}
    };

    inline LockedCRoot lockedCRoot() const { return LockedCRoot(*this); }

    // Use stringly typed JSON pointer (returns an invalid ref for malformed json pointers)
    // See https://rapidjson.org/md_doc_pointer.html#JsonPointer
    // and https://datatracker.ietf.org/doc/html/rfc6901
    // NULL is not allowed.
    // An empty string ("") returns the root, a valid json pointer ("/...") returns that,
    // and an invalid JSON pointer (one that starts not with '/') returns an invalid ref.
    // Don't forget to check if the returned ref is valid before accessing it.
    VarRef  subtree(const char *path, Var::SubtreeQueryFlags flags = Var::SQ_DEFAULT);
    VarCRef subtreeConst(const char* path) const;

    // Maintenance
    void fillExpiredSubnodes(std::vector<Var*>& v);

    Var _root;

    mutable acme::upgrade_mutex mutex;
};
