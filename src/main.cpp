#include <stdio.h>
#include <sstream>
#include <atomic>
#include <assert.h>
#include <signal.h>

#include "webserver.h"
#include "argh.h"

#include "config.h"
#include "handler_get.h"
#include "jsonstreamwrapper.h"
#include "json_in.h"
#include "teststuff.h"
#include "subproc.h"
#include "treefunc.h"
#include "util.h"

#ifndef SIGQUIT
#define SIGQUIT 3
#endif

std::atomic<bool> s_quit;

static void handlesigs(void (*f)(int))
{
    if(!f)
        f = SIG_DFL;
    signal(SIGBREAK, f);
    signal(SIGQUIT, f);
    signal(SIGTERM, f);
    signal(SIGINT, f);
}

static void sigquit(int)
{
    s_quit = true;
    handlesigs(NULL);
}

static void bail(const char *a, const char *b)
{
    fprintf(stderr, "FATAL: %s%s\n", a, b);
    _exit(1);
}


static bool loadcfg(DataTree& base, const char *fn)
{
    FILE* f = fopen(fn, "rb");
    if(!f)
        bail("Failed to open config file: ", fn);

    DataTree tree;
    char buf[4096];
    BufferedFILEReadStream fs(f, buf, sizeof(buf));
    bool ok = loadJsonDestructive(tree.root(), fs);
    fclose(f);

    if(!ok)
        bail("Error loading config file (bad json?): ", fn);

    if(!tree.root() || tree.root().type() != Var::TYPE_MAP)
        bail("Config file did not result in useful data, something is wrong: ", fn);

    return base.root().merge(tree.root(), MERGE_RECURSIVE | MERGE_APPEND_ARRAYS);
}

bool doargs(DataTree& tree, int argc, char **argv)
{
    ServerConfig cfg;
    argh::parser cmd;
    cmd.parse(argc, argv);

    for (size_t i = 1; i < cmd.size(); ++i)
    {
        printf("Loading config file: %s\n", cmd[i].c_str());
        loadcfg(tree, cmd[i].c_str());
    }

    return true;
}

int main(int argc, char** argv)
{
#ifdef _DEBUG
    teststuff();
#endif

    handlesigs(sigquit);

    DataTree cfgtree;
    if(!doargs(cfgtree, argc, argv))
        bail("Failed to handle cmdline. Exiting.", "");

    ServerConfig cfg;
    if(!cfg.apply(cfgtree.root()))
    {
        bail("Invalid config after processing options. Fix your config file(s).\nCurrent config:\n",
            dumpjson(cfgtree.root(), true).c_str()
        );
    }

    // Can start the webserver early while we're still loading up other things.
    WebServer::StaticInit();
    WebServer srv;
    if (!srv.start(cfg))
        bail("Failed to start server!", "");

    TreeHandler hcfg(cfgtree, 7); // strlen("/config")
    if(cfg.expose_debug_apis)
        srv.registerHandler("/config", hcfg.Handler, &hcfg);

    // TEST DATA START
    DataTree tree;
    {
        loadAndMergeJsonFromFile(&tree, "citylots.json", "/citylots", MERGE_FLAT);
        {
            AsyncLaunchConfig cfg;
            cfg.args.push_back("twitter.sh");
            loadAndMergeJsonFromProcess(&tree, std::move(cfg), "/twitter", MERGE_FLAT);
        }
        {
            AsyncLaunchConfig cfg;
            cfg.args.push_back("matrix.sh");
            loadAndMergeJsonFromProcess(&tree, std::move(cfg), "/matrix", MERGE_FLAT);
        }
    }
    // TEST DATA END


    // FIXME: this is ugly
    TreeHandler htree(tree, 4); // strlen("/get")
    srv.registerHandler("/get", htree.Handler, &htree);



    puts("Ready!");

    while (!s_quit)
        sleepMS(1000);

    srv.stop();
    WebServer::StaticShutdown();

    return 0;
}
