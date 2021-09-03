#include "view/viewparser.h"
#include "view/viewexec.h"
#include "util.h"
#include <stdio.h>
#include <assert.h>
#include "datatree.h"
#include "json_in.h"
#include "json_out.h"
#include "jsonstreamwrapper.h"
#include "debugfunc.h"
#include "view/view.h"

struct TestEntry
{
    const char *str;
    bool ok;
};

static const TestEntry tests[] =
{
    { "/hello/world", true },
    { "[name='test']/ids[*]", true },
    { "/hello/world[name='test']", true },
    { "/hello/world[name = 'test']", true },
    { "/hello/world[val=5]/", true },
    { "/hello/world[pi=3.1415]", true },
    { "/hello/world[nope=null]", true },
    { "/hello/world[s ?> '>']", true },
    { "/hello/world[s !?? 'secret']", true },
    { "/hello/world[nope=null]", true },
    //{ "/hello[$x]", true },   // TOOD: support this (use all in $x as key)
    //{ "/hello/world['/sub/key'=42]", true }, // probably still broken
    //{ "/rooms[name=$Q]/id", true },
    //{ "/users[room=${ids toint}]", true },
    //{ "${P/first_name} ${P/last_name}", true }, // not a query, doesn't accept this yet
};

// TODO: way to specify key exists, key not exists

void testload(DataTree& tree)
{
char json[] = R""(
{ "people": [
    { "name": "John (r1)", "room": 1 },
    { "name": "Jack (r1)", "room": 1 },
    { "name": "Pete (r2)", "room": 2 },
    { "name": "Zuck (r3)", "room": 3 }
],
    "rooms": [
    { "id": 1, "name": "Room #1", "open": true },
    { "id": 2, "name": "Room #2", "open": false },
    { "id": 3, "name": "Room #3", "open": true },
]
}
)"";

    InplaceStringStream in(&json[0], sizeof(json));
    bool ok = loadJsonDestructive(tree.root(), in);
    assert(ok);
}

Var testview(TreeMem& mem)
{
char json[] = R""(
{
    "lookup": {
        "ids": "/rooms[open=true]/id",
        "P" : "/users[room=$ids]"
    },
    "result" : "$P",
}
)"";

    Var ret;
    VarRef ref(mem, &ret);
    InplaceStringStream in(&json[0], sizeof(json));
    bool ok = loadJsonDestructive(ref, in);
    assert(ok);
    return ret;
}


static void dump(VarCRef ref)
{
    puts(dumpjson(ref, true).c_str());
}

static void disasm(const view::Executable& exe)
{
    std::vector<std::string> dis;
    exe.disasm(dis);
    for (size_t i = 1; i < dis.size(); ++i)
        printf("  [%u] %s\n", unsigned(i), dis[i].c_str());
}

void testview()
{
    DataTree tree;
    testload(tree);
    View vw(tree);
    Var viewdata = testview(tree);
    vw.load(VarCRef(tree, &viewdata));
    disasm(vw.exe);

    view::VM vm(vw.exe, vw.ep.data(), vw.ep.size());
    bool ok = vm.run(tree.root());
    assert(ok);

    const view::VarRefs& out = vm.results();

    printf("VM out: %u vars\n", (unsigned)out.size());
    for (const VarCRef& v : out)
    {
        dump(v);
        puts("-----");
    }

    viewdata.clear(tree);
}

void testparse()
{
    bool allok = true;
    TreeMem mem;
    for (size_t i = 0; i < Countof(tests); ++i)
    {
        view::Executable exe(mem);
        size_t start = view::parse(exe, tests[i].str);
        bool ok = !!start == tests[i].ok;
        printf("%s:%s: %s\n", ok ? "GOOD" : "FAIL", tests[i].ok ? "valid  " : "invalid", tests[i].str);
        allok = allok && ok;
        if (start)
            disasm(exe);
    }
    printf("### all good = %d\n", allok);
}

void testexec()
{
    DataTree tree;
    testload(tree);
    //dump(tree.subtree("/rooms"));

    TreeMem exm;
    view::Executable exe(exm);
    size_t start = view::parse(exe, "/rooms[open=true]/id");
    assert(start);
    disasm(exe);

    view::VM vm(exe, NULL, 0);
    vm.run(tree.root());
    const view::VarRefs& out = vm.results();

    printf("VM out: %u vars\n", (unsigned)out.size());
    for (const VarCRef& v : out)
    {
        dump(v);
        puts("-----");
    }

}

int main(int argc, char** argv)
{
    //testparse();
    //testexec();
    testview();


    return 0;
}
