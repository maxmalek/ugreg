#include <stdio.h>
#include <vector>
#include "tinyhashmap.h"
#include "variant.h"
#include "treemem.h"

#define SHOWSIZE(x) printf("%s = %u\n", (#x), (unsigned)sizeof(x))

int main(int argc, char **argv)
{
    tinyhashmap_api_test();

    typedef LVector<Var, u32, Var::Policy> LVector_Var;

    typedef TinyHashMap<Var, Var::Policy> TinyHashMap_Var;

    SHOWSIZE(LVector_Var);

    SHOWSIZE(TinyHashMap_Var);
    SHOWSIZE(Var);
    SHOWSIZE(Var::Map);

    typedef HashHatKeyStore<u32> HashHatKeyStore_u32;
    SHOWSIZE(HashHatKeyStore_u32);
    SHOWSIZE(HashHatKeyStore_u32::Bucket);



    BlockAllocator mem;

    TinyHashMap<unsigned> hm;
    for(unsigned N = 0; N < 10000; ++N)
    {
        //printf("%u ... ", N);
        hm.clear(mem);
        for(unsigned i = 0; i < N; ++i)
        {
            assert(hm.size() == i);
            hm.at(mem, StrRef(i)+1) = i + 'a';
            assert(hm.size()-1 == i);
        }
        
        assert(hm.size() == N);

        for (unsigned i = 0; i < N*2; ++i)
        {
            unsigned *p = hm.getp(StrRef(i) + 1);
            if(i < N)
                assert(*p == i + 'a');
            else
                assert(!p);
        }

        //printf("ok\n");
    }

    hm.dealloc(mem);

    return 0;
}

