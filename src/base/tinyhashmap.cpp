#include "tinyhashmap.h"
#include <assert.h>
#include "treemem.h"
#include "variant.h"

// all of the API exposed by the hashmap, to catch possible compile errors
// and API mismatches
[[maybe_unused]]
void tinyhashmap_api_test()
{
    TreeMem mem;
    typedef TinyHashMap<Var> M;
    M m;

    assert(m.empty());

    m.insert(mem, 42, std::move(Var(u64(6581))));
    M::InsertResult ins = m.insert_new(mem, 42);
    ins.ref.setFloat(mem, -1.0f);

    assert(m.size() == 1);

    m.at(mem, 23) = "blarg";

    Var *p = m.getp(42);

    u64 x = 0;
    for(M::const_iterator it = m.begin(); it != m.end(); ++it)
        x += it.key();

    for(M::iterator it = m.begin(); it != m.end(); ++it)
        it.value().clear(mem);

    M m2 = std::move(m);

    m2.dealloc(mem);
}
