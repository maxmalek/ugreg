#include "treemem.h"
#include <assert.h>


TreeMem::TreeMem()
{
}

TreeMem::~TreeMem()
{
}

TreeMemReadLocker::TreeMemReadLocker(TreeMem& mem)
    : mem(mem), lock(mem.mutex)
{
}

TreeMemReadLocker::~TreeMemReadLocker()
{
}
