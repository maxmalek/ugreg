#include "serverutil.h"
#include <atomic>
#include <signal.h>
#include <stdio.h>
#include "argh.h"

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
        bail("Error loading config file (bad json?): ", fn);

    if (!tree.root() || tree.root().type() != Var::TYPE_MAP)
        bail("Config file did not result in useful data, something is wrong: ", fn);

    return base.root().merge(tree.root(), MERGE_RECURSIVE | MERGE_APPEND_ARRAYS);
}

bool doargs(DataTree& tree, int argc, char** argv)
{
    argh::parser cmd;
    cmd.parse(argc, argv);

    for (size_t i = 1; i < cmd.size(); ++i)
    {
        printf("Loading config file: %s\n", cmd[i].c_str());
        loadcfg(tree, cmd[i].c_str());
    }

    return true;
}
