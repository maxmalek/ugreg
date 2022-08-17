#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "serverutil.h"
#include <atomic>
#include <signal.h>
#include <stdio.h>
#include <assert.h>
#include <sstream>

#include "datatree.h"
#include "util.h"
#include "jsonstreamwrapper.h"
#include "json_in.h"

#ifndef SIGQUIT
#define SIGQUIT 3
#endif

void handlesigs(void (*f)(int))
{
    if (!f)
        f = SIG_DFL;
#ifdef SIGBREAK
    signal(SIGBREAK, f);
#endif
    signal(SIGQUIT, f);
    signal(SIGTERM, f);
    signal(SIGINT, f);
}

[[noreturn]]
void bail(const char* a, const char* b)
{
    fprintf(stderr, "FATAL: %s%s\n", a, b);
    exit(1);
}


static bool loadcfg(DataTree& base, const char* fn)
{
    FILE* f = fopen(fn, "rb");
    if (!f)
    {
        bail("Failed to open config file: ", fn);
        return false;
    }

    DataTree tree;
    char buf[4096];
    BufferedFILEReadStream fs(f, buf, sizeof(buf));
    bool ok = loadJsonDestructive(tree.root(), fs);
    fclose(f);

    if (!ok)
    {
        printf("Error loading config file (bad json?): %s\n", fn);
        printf("Stream pos right now is %zu, buffer follows:\n", fs.Tell());
        unsigned i = 0;
        char c;
        std::ostringstream os;
        while (((c = fs.Take())) && i++ < 100)
            os << c;
        puts(os.str().c_str());

        bail("Exiting.", "");
    }

    if (!tree.root() || tree.root().type() != Var::TYPE_MAP)
        bail("Config file did not result in useful data, something is wrong: ", fn);

    return base.root().merge(tree.root(), MERGE_RECURSIVE | MERGE_APPEND_ARRAYS);
}

bool doargs(DataTree& tree, int argc, char** argv, ArgsCallback cb, void *ud)
{
    bool parseSwitches = true;
    for (int i = 1; i < argc; )
    {
        const char *arg = argv[i];
        if(parseSwitches && arg[0] == '-')
        {
            if(arg[1] == '-')
            {
                if(!arg[2])
                {
                    parseSwitches = false;
                    ++i;
                    continue;
                }
            }

            size_t fwd = cb ? cb(argv, i, ud) : 0;

            if(!fwd)
            {
                printf("Error: Unhandled switch: %s\n", arg);
                return false;
            }
            assert(i + fwd <= argc && "You sure you handled this many args?");
            i += fwd;
        }
        else
        {
            printf("Loading config file: %s\n", arg);
            if(!loadcfg(tree, arg))
                return false;
            ++i;
        }
    }

    return true;
}
