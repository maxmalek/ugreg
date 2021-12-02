#include "viewparser.h"
#include "viewexec.h"
#include "util.h"
#include <stdio.h>
#include <assert.h>
#include "datatree.h"
#include "json_in.h"
#include "json_out.h"
#include "jsonstreamwrapper.h"
#include "debugfunc.h"
#include "view.h"

using namespace view;

struct TestEntry
{
    const char *str;
    bool ok;
};

static const TestEntry tests[] =
{
    // negative tests (should fail to parse)
    //{ "${./hello/world[name=this_should_fail]}", false }, // expected literal or var
    //{ "${./hello/world[this_should_fail}", false },       // missing =
    //{ "${./hello/world['this_should_fail'}", false },     // unterminated [
    //{ "${./hello/world['this_should_fail}", false },      // unterminated '
    //{ "${./hello/world[this should fail = 0]}", false },  // spaces in identifier not allowed unless it's a string literal

    // positive tests
    { "${.}", true },
    { "${./hello/world}", true },
    { "${.[*]}", true },
    { "${.|unpack}", true },
    { "${./hello[*]}", true },
    { "${.[name='test']}", true },
    { "${.[name='test']/ids[*]}", true },
    { "${./hello/world[name='test']}", true },
    { "${./hello/world[name = 'test']}", true },
    { "${./hello/world[val=5]/''}", true },
    { "${./hello/world[pi=3.1415]}", true },
    { "${./hello/world[nope=null]}", true },
    { "${./hello/world[s ?> '>']}", true },
    { "${./hello/world[s !?? 'secret']}", true },
    { "${./hello/world[nope=null]}", true },
    { "${./hello/world['this is fine'=0]}", true },
    { "${$x/subkey}", true },
    { "${$x[val=42]}", true },
    { "string $with var", true },
    { "just ;$a string, and $one var", true },
    { "$func(0)", true},
    { "$func(0, x)", false},
    { "${func(0) / subkey /'with space' | unpack | array | test(true)}", true},
    {"$toint(42)", true},
    {"${toint(42)}", true},
    {"${'42'|toint}", true},
    {"${toint('42')}", true},
    { "${$ROOT/path/to[name == $ROOT/validnames | tolower]}", true},
    { "${$ROOT/path/to[name == f($ROOT/validnames | tolower, '42'|toint, 'str')]}", true},

    //{ "{/hello[$x]}", true },   // TODO: support this? (use all in $x as key)
    // ^ not sure if we should. that would introduce a data-based lookup.

    /*{ "{/hello/world['/sub/key'=42]}", true }, // valid but misleading (CHECKKEY only)
    { "{/hello/world[{/sub/key}=42]}", true }, // proper sub-sub-key selection
    { "{/rooms[name=$Q]/id}", true },
    { "{/users[room=${ids toint}]}", true },*/
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
        "P" : "/people[room=$ids]/name"
    },
    "result" : "${P compact array}",
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
        puts(dis[i].c_str());
}

void testview()
{
    DataTree tree;
    testload(tree);
    View vw(tree);
    Var viewdata = testview(tree);
    vw.load(VarCRef(tree, &viewdata));
    disasm(vw.exe);

    view::VM vm(tree);
    vm.init(vw.exe, vw.ep.data(), vw.ep.size());
    bool ok = vm.run(tree.root());
    assert(ok);

    const view::VarRefs& out = vm.results();

    printf("VM out: %u vars\n", (unsigned)out.size());
    for (const VarEntry& e : out)
    {
        printf("[%s]\n", vm.mem.getS(e.key));
        dump(e.ref);
    }

    viewdata.clear(tree);
}

void testparse()
{
    bool allok = true;
    TreeMem mem;
    std::string errs;
    std::vector<size_t> failed;
    for (size_t i = 0; i < Countof(tests); ++i)
    {
        view::Executable exe(mem);
        size_t start = view::parse(exe, tests[i].str, errs);
        bool ok = !!start == tests[i].ok;
        if(!ok)
            failed.push_back(i);
        printf("%s:%s: %s\n", ok ? "GOOD" : "FAIL", tests[i].ok ? "valid  " : "invalid", tests[i].str);
        allok = allok && ok;
        if (start)
            disasm(exe);
    }
    if(errs.length())
        printf("Parse errors:\n%s\n", errs.c_str());
    if(allok)
        puts("All good!");
    else
    {
        puts("!! Tests failed:");
        for(size_t i = 0; i < failed.size(); ++i)
            puts(tests[failed[i]].str);
    }
}

void testexec()
{
    DataTree tree;
    testload(tree);
    //dump(tree.subtree("/rooms"));

    std::string err;
    TreeMem exm;
    view::Executable exe(exm);
    size_t start = view::parse(exe, "/rooms[open=true]/id", err);
    if(!start)
    {
        printf("Parse error:\n%s\n", err.c_str());
        return;
    }
    assert(start);
    disasm(exe);

    view::VM vm(exm);
    vm.init(exe, NULL, 0);
    vm.run(tree.root());
    const view::VarRefs& out = vm.results();

    printf("VM out: %u vars\n", (unsigned)out.size());
    for (const VarEntry& e : out)
    {
        printf("[%s]\n", vm.mem.getS(e.key));
        dump(e.ref);
    }

}

int main(int argc, char** argv)
{
    testparse();
    //testexec();
    //testview();


    return 0;
}
