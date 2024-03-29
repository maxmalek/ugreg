#include <assert.h>
#include <string>
#include "rapidjson/stringbuffer.h"
#include "datatree.h"
#include "jsonstreamwrapper.h"
#include "json_in.h"
#include "json_out.h"
#include "accessor.h"
#include "pathiter.h"
#include "webstuff.h"

// Misc things to test for functionality, breakage, and to make sure everything compiles as it should

static void jsonout(VarCRef ref)
{
    puts(dumpjson(ref).c_str());
}

static void testpathiter()
{
    {
        const char* path = "/";
        PathIter it(path);
        assert(it.value().len == 0 && !*it.value().s);
        assert(!it.hasNext());
    }

    {
        const char *path = "/a/bb/ccc/";
        PathIter it(path);
        assert(it.value().len == 1 && !strncmp(it.value().s, "a", 1));
        assert(it.hasNext());
        ++it;
        assert(it.value().len == 2 && !strncmp(it.value().s, "bb", 2));
        assert(it.hasNext());
        ++it;
        assert(it.value().len == 3 && !strncmp(it.value().s, "ccc", 3));
        assert(it.hasNext());
        ++it;
        assert(it.value().len == 0 && !*it.value().s);
        assert(!it.hasNext());
    }
}

static void testtree()
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
        const double *v = tree.subtree("/d/1").asFloat();
        printf("ptr: /d/1 = %f\n", *v);
    }

    puts("---- test stuff end -----");
}

static void testweb()
{
    URLTarget t;
    bool ok = t.parse("https://example.com:8080/page.html");
    assert(ok);
    assert(t.port == 8080);
    assert(t.host == "example.com");
    assert(t.path == "/page.html");
}

int main(int argc, char **argv)
{
    testpathiter();
    testtree();
    testweb();
    return 0;
}
