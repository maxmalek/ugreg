#include "teststuff.h"
#include <assert.h>
#include "rapidjson/stringbuffer.h"
#include "datatree.h"
#include "jsonstreamwrapper.h"
#include "json_in.h"
#include "json_out.h"
#include "accessor.h"

// Misc things to test for functionality, breakage, and to make sure everything compiles as it should

static void jsonout(VarCRef ref)
{
    puts(dumpjson(ref).c_str());
}

void teststuff()
{
    DataTree tree;
    {
        char json[] = " { \"a\":[1, 2, 3, [], {}, [0]], \"b\":null, \"c\":123.45 } ";
        //char json[] = " { \"a\":[1, 2, 3, [], {}, [0]] } ";
        char json2[] = "{ \"b\":\"BBB\", \"c\":[0], \"d\":[{}, 987.123] }";
        InplaceStringStream jsonss(&json[0], sizeof(json));
        InplaceStringStream jsonss2(&json2[0], sizeof(json2));

        DataTree extra;
        bool ok2 = loadJsonDestructive(extra.root(), jsonss2);
        assert(ok2);

        bool ok = loadJsonDestructive(tree.root(), jsonss);
        assert(ok);

        extra.root()["e"]["first"] = "<---";
        tree.root()["e"]["second"] = "--->";

        jsonout(tree.root());
        jsonout(extra.root());


        tree.root().merge(extra.root(), MERGE_RECURSIVE);
        jsonout(tree.root());
    }

    {
        Accessor acc(tree, "d", 1);
        const double* v = tree.subtree(acc).asFloat();
        printf("acc: json.d.1 = %f\n", *v);

        v = tree.subtree("/d/1").asFloat();
        printf("ptr: /d/1 = %f\n", *v);
    }

    puts("---- test stuff end -----");
}
