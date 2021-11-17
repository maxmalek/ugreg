/* Backing memory for a tree of Var things.
Has optimizations for:
- Strings are deduplicated and stored in a string pool
- Small memory blocks are allocated using a block allocator

Warning: NOT thread safe!
Unlike malloc/free, this class' Free() needs the size, and it must be the original size as
passed to the allocation.
*/

#pragma once

#include "types.h"
#include "containers.h"
#include <shared_mutex>

struct LuaAlloc;

class TreeMem : public BlockAllocator, public StringPool
{
public:
    TreeMem();
    ~TreeMem();
    mutable std::shared_mutex mutex;
};

// Locks TreeMem for reading (which can be further locked for writing)
class TreeMemReadLocker
{
public:
    TreeMemReadLocker(TreeMem& mem);
    ~TreeMemReadLocker();

    TreeMem& mem;
    inline std::shared_mutex& mutex() { return mem.mutex; }

    const std::shared_lock<std::shared_mutex> lock;
};
