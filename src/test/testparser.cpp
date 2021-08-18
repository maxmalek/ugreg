#include "view/viewparser.h"
#include "util.h"
#include <stdio.h>

struct TestEntry
{
    const char *str;
    bool ok;
};

static const TestEntry tests[] =
{
    { "/hello/world", true },
    { "[name='test']", true },
    { "/hello/world[name='test']", true },
    { "/hello/world[name = 'test']", true },
    { "/hello/world[val=5]/", true },
    { "/hello/world[pi=3.1415]", true },
    { "/hello/world[nope=null]", true },
    { "/hello/world[s ?> '>']", true },
    { "/hello/world[s !?? 'secret']", true },
    { "/hello/world[nope=null]", true },
    //{ "/hello/world[/sub/key=42]", true }, // probably still broken
    { "/rooms[name=$Q]/id", true },
    { "/users[room=${ids toint}]", true },
    //{ "${P/first_name} ${P/last_name}", true }, // not a query, doesn't accept this yet
};


int main(int argc, char** argv)
{
    bool allok = true;
    for(size_t i = 0; i < Countof(tests); ++i)
    {
        bool ok = parseView(tests[i].str) == tests[i].ok;
        printf("%s:%s: %s\n", ok ? "GOOD" : "FAIL", tests[i].ok ? "valid  " : "invalid", tests[i].str);
        allok = allok && ok;
    }
   
    printf("### all good = %d\n", allok);
    return 0;
}
