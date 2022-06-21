// maiden main

#include <stdio.h>
#include <sstream>
#include <atomic>
#include <assert.h>
#include <time.h>

#include "webserver.h"
#include "config.h"
#include "util.h"
#include "viewmgr.h"
#include "serverutil.h"
#include "datatree.h"
#include "json_out.h"
#include "mxstore.h"
#include "handler_mxid_v2.h"
#include <civetweb/civetweb.h>
#include "subproc.h"
#include "subprocess.h"

std::atomic<bool> s_quit;

static void sigquit(int)
{
    s_quit = true;
    handlesigs(NULL);
}

// mg_request_handler
int handler_versions(struct mg_connection* conn, void*)
{
    // we only support v2 so this can be hardcoded for now
    static const char ver[] = "{\"versions\":[\"v1.2\"]}";
    mg_send_http_ok(conn, "application/json", sizeof(ver) - 1);
    mg_write(conn, ver, sizeof(ver) - 1); // don't include trailing \0
    return 200;
}

static std::vector<std::string> s_envStrings;
static std::vector<const char*> s_envPtrs;

static void updateEnv(VarCRef xenv)
{
    s_envStrings.clear();
    s_envPtrs.clear();

    const Var::Map *m = xenv.v->map();
    if(!m)
        return;

    std::string tmp;
    for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
    {
        const char *k = xenv.mem->getS(it.key());
        const char *v = it.value().asCString(*xenv.mem);
        if(k && v)
        {
            tmp = k;
            tmp += '=';
            tmp += v;
            printf("ENV: %s\n", tmp.c_str());
            s_envStrings.push_back(std::move(tmp));
        }
    }

    // HACK HAAAAACK
    if(const char *path = getenv("PATH"))
        s_envStrings.push_back(std::string("PATH=") + path);


    const size_t n = s_envStrings.size();
    s_envPtrs.reserve(n+1);
    for(size_t i = 0; i < n; ++i)
        s_envPtrs.push_back(s_envStrings[i].c_str());
    s_envPtrs.push_back(NULL);
}

// TODO: add config env support
static bool runAndMerge(MxStore& mxs, const char *proc)
{
    DataTree tree;
    const char *args[] = { proc, NULL };
    bool ok = loadJsonFromProcess(tree.root(), args, s_envPtrs.data(),
        subprocess_option_enable_async | subprocess_option_no_window);

    if(!ok)
        return false;

    return mxs.merge3pid(tree.root());
}

int main(int argc, char** argv)
{
    hash_testall();
    srand(unsigned(time(NULL)));
    handlesigs(sigquit);

    MxStore mxs;
    ServerConfig cfg;
    {
        DataTree cfgtree;
        if (!doargs(cfgtree, argc, argv))
            bail("Failed to handle cmdline. Exiting.", "");

        if (!cfg.apply(cfgtree.subtree("/config")))
        {
            bail("Invalid config after processing options. Fix your config file(s).\nCurrent config:\n",
                dumpjson(cfgtree.root(), true).c_str()
            );
        }
        if(!mxs.apply(cfgtree.subtree("/matrix")))
            bail("Invalid matrix config. Exiting.", "");

        updateEnv(cfgtree.subtree("/env"));

        // FIXME TESTING ONLY
        runAndMerge(mxs, "3pid\\fakeusers.bat");
        runAndMerge(mxs, "3pid\\fakestud.bat");

        mxs.rotateHashPepper();
    }

    WebServer::StaticInit();
    WebServer srv;
    if (!srv.start(cfg))
        bail("Failed to start server!", "");

    srv.registerHandler("/_matrix/identity/versions", handler_versions, NULL);

    MxidHandler_v2 v2(mxs, "/_matrix/identity/v2");
    srv.registerHandler(v2);

    puts("Ready!");

    while (!s_quit)
        sleepMS(200);

    srv.stop();
    WebServer::StaticShutdown();
    //mxs.save();

    return 0;
}
