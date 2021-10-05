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

struct LuaAlloc;

class TreeMem : public BlockAllocator, public StringPool
{
public:
    TreeMem();
    ~TreeMem();
};
