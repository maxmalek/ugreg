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
#include "json_out.h"
#include "subproc.h"
#include "treefunc.h"
#include "util.h"
#include "handler_view.h"
#include "viewmgr.h"
#include "handler_debug.h"
#include "fetcher.h"

#ifndef SIGQUIT
#define SIGQUIT 3
#endif

std::atomic<bool> s_quit;

static void handlesigs(void (*f)(int))
{
    if(!f)
        f = SIG_DFL;
#ifdef SIGBREAK
    signal(SIGBREAK, f);
#endif
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
    exit(1);
}


static bool loadcfg(DataTree& base, const char *fn)
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
    handlesigs(sigquit);

    DataTree cfgtree;
    if(!doargs(cfgtree, argc, argv))
        bail("Failed to handle cmdline. Exiting.", "");

    ServerConfig cfg;
    if(!cfg.apply(cfgtree.subtree("/config")))
    {
        bail("Invalid config after processing options. Fix your config file(s).\nCurrent config:\n",
            dumpjson(cfgtree.root(), true).c_str()
        );
    }

    view::Mgr vmgr;
    {
        VarCRef views = cfgtree.subtree("/view");
        if(const Var::Map *m = views ? views.v->map() : NULL)
            for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
            {
                const char *key = cfgtree.getS(it.key());
                printf("Adding view [%s] ...\n", key);
                if(vmgr.addViewDef(key, VarCRef(cfgtree, &it.value())))
                    printf("-> OK\n");
                else
                    printf("-> FAILED\n");
            }
    }

    // Can start the webserver early while we're still loading up other things.
    WebServer::StaticInit();
    WebServer srv;
    if (!srv.start(cfg))
        bail("Failed to start server!", "");

    TreeHandler hcfg(cfgtree, "/config", cfg);
    if(cfg.expose_debug_apis)
        srv.registerHandler(hcfg);

    // TEST DATA START
    DataTree tree;

    // register fetchers
    if(VarCRef fetch = cfgtree.subtree("/fetch"))
        if(const Var::Map *m = fetch.v->map())
            for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
            {
                const char *path = fetch.mem->getS(it.key());
                printf("Init fetcher [%s] ...\n", path);
                if(Fetcher *f = Fetcher::New(cfgtree, VarCRef(cfgtree, &it.value())))
                    if(VarRef dst = tree.subtree(path, true))
                        dst.v->makeMap(tree)->ensureExtra(tree)->fetcher = f;
            }

    InfoHandler hinfo(tree, "/info");
    srv.registerHandler(hinfo);

    DebugStrpoolHandler hstrpool(tree, "/debug/strings");
    if (cfg.expose_debug_apis)
        srv.registerHandler(hstrpool);

    ViewHandler hview(vmgr, tree, "/view", cfg);
    hview.setupCache(cfg.reply_cache.rows, cfg.reply_cache.columns, cfg.reply_cache.maxtime);
    srv.registerHandler(hview);

    ViewDebugHandler htestview(tree, "/testview", cfg);
    if (cfg.expose_debug_apis)
        srv.registerHandler(htestview);

    {
        //loadAndMergeJsonFromFile(&tree, "test/citylots.json", "/citylots", MERGE_FLAT);
        loadAndMergeJsonFromFile(&tree, "test/mock_users.json", "/users", MERGE_FLAT);
        loadAndMergeJsonFromFile(&tree, "test/mock_rooms.json", "/rooms", MERGE_FLAT);
        {
            AsyncLaunchConfig cfg;
            cfg.args.push_back("./twitter.sh");
            loadAndMergeJsonFromProcess(&tree, std::move(cfg), "/twitter", MERGE_FLAT);
        }
        /*{
            AsyncLaunchConfig cfg;
            cfg.args.push_back("./matrix.sh");
            loadAndMergeJsonFromProcess(&tree, std::move(cfg), "/matrix", MERGE_FLAT);
        }*/
    }
    // TEST DATA END

    TreeHandler htree(tree, "/get", cfg);
    htree.setupCache(cfg.reply_cache.rows, cfg.reply_cache.columns, cfg.reply_cache.maxtime);
    srv.registerHandler(htree);

    DebugCleanupHandler hclean(tree, "/debug/cleanup");
    hclean.addForCleanup(&htree);
    hclean.addForCleanup(&hview);
    if (cfg.expose_debug_apis)
        srv.registerHandler(hclean);

    puts("Ready!");

    while (!s_quit)
        sleepMS(200);

    srv.stop();
    WebServer::StaticShutdown();

    return 0;
}
