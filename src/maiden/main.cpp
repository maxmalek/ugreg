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
#include "mxsources.h"
#include "mxservices.h"
#include "scopetimer.h"

std::atomic<bool> s_quit;

static void sigquit(int)
{
    s_quit = true;
    handlesigs(NULL);
}

//static std::string sDumpFn;
//static bool sDump;

static const char *usage =
"Usage: ./maiden <switches> config1.json configN.json <switches> ...\n"
"Supported switches:\n"
"-h --help    This help\n"
//"--dump       Dump 3pid storage to stdout after populating it\n"
//"--dump=file  Dump 3pid storage to file instead\n"
"--exit       Exit successfully after loading instead of starting webserver\n"
"--           Stop parsing switches"
"";

static size_t argsCallback(char **argv, size_t idx, void* ud)
{
    const char* sw = argv[idx];
    while (*sw && *sw == '-')
        ++sw;
    if (!strcmp(sw, "help") || !strcmp(sw, "h"))
    {
        puts(usage);
        exit(0);
    }
    /*else if (!strncmp(sw, "dump", 4))
    {
        sDump = true;
        sw += 4;
        if (*sw == '=')
            sDumpFn = sw + 1;
        return 1;
    }*/
    else if (!strcmp(sw, "exit"))
    {
        s_quit = true;
        return 1;
    }
    return 0;
}

/*static void dump(MxStore& mxs)
{
    FILE* out = stdout;
    bool close = false;
    if (sDumpFn.empty())
        puts("--- BEGIN 3PID DUMP ---");
    else
    {
        out = fopen(sDumpFn.c_str(), "w");
        close = true;
    }
    if (!out)
        bail("Failed to open --dump file: ", sDumpFn.c_str());

    {
        char buf[4 * 1024];
        BufferedFILEWriteStream ws(out, buf, sizeof(buf));
        DataTree::LockedRoot lockroot = mxs.get3pidRoot();
        writeJson(ws, lockroot.ref, true);
        ws.Flush();
    }

    if(close)
        fclose(out);
    else
        puts("--- END 3PID DUMP ---");
}*/

// mg_request_handler, part of identity server
static const char * const URL_versions = "/_matrix/identity/versions";
static int handler_versions(struct mg_connection* conn, void*)
{
    // we only support v2 so this can be hardcoded for now
    static const char ver[] = "{\"versions\":[\"v1.2\"]}";
    mg_send_http_ok(conn, "application/json", sizeof(ver) - 1);
    mg_write(conn, ver, sizeof(ver) - 1); // don't include trailing \0
    return 200;
}

// mg_request_handler, enabled if fake_v1 is enabled
static const char * const URL_identity_v1_min = "/_matrix/identity/api/v1";
static int handler_identity_v1_min(struct mg_connection *conn, void *)
{
    static const char resp[] = "{}";
    mg_send_http_ok(conn, "application/json", sizeof(resp) - 1);
    mg_response_header_add(conn, "Access-Control-Allow-Origin", "*", 1);
    mg_write(conn, resp, sizeof(resp) - 1); // don't include trailing \0
    return 200;
}


class ServerAndConfig
{
public:
    static ServerAndConfig *New(VarCRef json)
    {
        ServerAndConfig *ret = new ServerAndConfig;
        if(!ret->cfg.apply(json))
        {
            delete ret;
            ret = NULL;
        }
        return ret;
    }
    WebServer srv;
    ServerConfig cfg;

    bool start()
    {
        return srv.start(cfg);
    }

    void stop()
    {
        srv.stop();
    }

private:
    ServerAndConfig() {}
};

WebServer *prepareServer(std::vector<ServerAndConfig*> servers, RequestHandler *h, VarCRef cfg)
{
    ServerAndConfig *sc = ServerAndConfig::New(cfg);
    if(!sc)
        return NULL;
    sc->srv.registerHandler(*h);
    servers.push_back(sc);
    return &sc->srv;
}

static MxidHandler_v2 *identity;
static MxSearchHandler *search;
static MxWellknownHandler *wellknown;
static MxStore *mxs; // needed by identity

static bool initServices(std::vector<ServerAndConfig*> servers, MxSources& sources, VarCRef cfg)
{
    if(VarCRef x = cfg.lookup("identity"))
    {
        hash_testall();

        if(VarCRef x3pid = cfg.lookup("3pid"))
        {
            mxs = new MxStore;
            if(!mxs->apply(x3pid))
                return false;
        }
        if(!mxs)
        {
            logerror("Identity service requires 3pid store; make sure this is correctly configured");
            return false;
        }
        identity = new MxidHandler_v2(*mxs, "/_matrix/identity/v2");
        if(!identity->init(x))
            return false;
        if(WebServer *ws = prepareServer(servers, identity, x))
        {
            ws->registerHandler(URL_versions, handler_versions, NULL);
            if(identity->fake_v1)
                ws->registerHandler(URL_identity_v1_min, handler_identity_v1_min, NULL);
        }
    }

    if(VarCRef x = cfg.lookup("usersearch"))
    {
        search = new MxSearchHandler(sources);
        if(!search->init(x))
            return false;
        if(!prepareServer(servers, search, x))
            return false;
    }

    if(VarCRef x = cfg.lookup("wellknown"))
    {
        wellknown = new MxWellknownHandler;
        if(!wellknown->init(x))
            return false;
        if(!prepareServer(servers, wellknown, x))
            return false;
    }

    return true;
}


static bool startServers(const std::vector<ServerAndConfig*>& srv)
{
    for(size_t i = 0; i < srv.size(); ++i)
        if(!srv[i]->start())
            return false;
    return true;
}

static void stopAndDelete(ServerAndConfig *s)
{
    s->srv.stop();
    delete s;
}
static void stopServers(std::vector<ServerAndConfig*>&& srv)
{
    // parallel shutdown to save time
    std::vector<std::future<void> > tmp;
    for (size_t i = 0; i < srv.size(); ++i)
        tmp.push_back(std::move(std::async(std::launch::async, stopAndDelete, srv[i])));
    srv.clear();
}


static int main2(MxSources& sources, int argc, char** argv)
{
    ScopeTimer timer;

    std::vector<ServerAndConfig*> servers;

    {
        DataTree cfgtree(DataTree::TINY);
        if (!doargs(cfgtree, argc, argv, argsCallback, NULL))
            bail("Failed to handle cmdline. Exiting.", "");


        if(!sources.initConfig(cfgtree.subtree("/sources"), cfgtree.subtree("/env")))
            bail("Invalid sources config. Exiting.", "");

        // ... then init all servers, but don't start them...
        if(!initServices(servers, sources, cfgtree.root()))
            bail("Failed to init services. Exiting.", "");
    }

    const bool loaded = sources.load();

    // need data to operate on. this takes a while so if we already have data, start up with those
    if(loaded)
        log("Loaded cached data; doing fast startup by skipping pre-start populate");
    else
        sources.initPopulate(false);

    if(!s_quit)
    {
        if(mxs)
            mxs->rotateHashPepper();

        WebServer::StaticInit();

        if(!startServers(servers))
            logerror("Failed to start a server component, exiting", "");

        log("Ready; all servers up after %u ms", (unsigned)timer.ms());

        // If we have the cached data, we can slowly start loading in new data in background
        if(loaded)
            sources.initPopulate(true);

        while (!s_quit)
            sleepMS(200);

        log("Main loop exited, shutting down...");
        stopServers(std::move(servers));
        WebServer::StaticShutdown();
    }

    return 0;
}

int main(int argc, char** argv)
{
    srand(unsigned(time(NULL)));
    handlesigs(sigquit);

    MxSources sources;
    int ret = main2(sources, argc, argv);

    sources.save();
    log("Exiting.");
    return ret;
}
