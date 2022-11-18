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
static bool sExit;

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
        sExit = true;
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

// mg_request_handler, part of "identity_v2"
static const char * const URL_versions = "/_matrix/identity/versions";
static int handler_versions(struct mg_connection* conn, void*)
{
    // we only support v2 so this can be hardcoded for now
    static const char ver[] = "{\"versions\":[\"v1.2\"]}";
    mg_send_http_ok(conn, "application/json", sizeof(ver) - 1);
    mg_write(conn, ver, sizeof(ver) - 1); // don't include trailing \0
    return 200;
}

// mg_request_handler, part of "identity_v1_min"
static const char * const URL_identity_v1_min = "/_matrix/identity/api/v1";
static int handler_identity_v1_min(struct mg_connection *conn, void *ud)
{
    static const char resp[] = "{}";
    mg_send_http_ok(conn, "application/json", sizeof(resp) - 1);
    mg_response_header_add(conn, "Access-Control-Allow-Origin", "*", 1);
    mg_write(conn, resp, sizeof(resp) - 1); // don't include trailing \0
    return 200;
}

static bool attachService(WebServer& srv, const char *name, MxStore& mxs, VarCRef serviceConfig)
{
    printf("attach service [%s]\n", name);

    RequestHandler *h = NULL;

    if(!strcmp(name, "versions"))
    {
        srv.registerHandler(URL_versions, handler_versions, NULL);
    }
    else if(!strcmp(name, "identity_v2"))
    {
        h = new MxidHandler_v2(mxs, "/_matrix/identity/v2");
    }
    else if(!strcmp(name, "identity_v1_min"))
    {
        srv.registerHandler(URL_identity_v1_min, handler_identity_v1_min, NULL);
    }
    else if(!strcmp(name, "search"))
    {
        h = new MxSearchHandler(mxs, serviceConfig);
    }
    else if(!strcmp(name, "wellknown"))
    {
        h = new MxWellknownHandler(serviceConfig);
    }
    else if(!strcmp(name, "hsproxy"))
    {
        //h = new MxReverseProxyHandler;
    }
    else
        bail("Unkown service: ", name);

    if(h)
        srv.registerHandler(h, true);

    return true;
}

class ServerAndConfig
{
public:
    static ServerAndConfig *New(VarCRef json, MxStore& mxs)
    {
        ServerAndConfig *ret = new ServerAndConfig;
        bool ok = true;
        if(ret->cfg.apply(json))
        {
            VarCRef servicesRef = json.lookup("services");
            const Var::Map *m = servicesRef.v->map();
            for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
            {
                const char *serviceName = servicesRef.mem->getS(it.key());
                VarCRef serviceConfig(servicesRef.mem, &it.value());
                if(!attachService(ret->srv, serviceName, mxs, serviceConfig))
                {
                    ok = false;
                    break;
                }
            }
        }
        if(!ok)
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

static void stopAndDelete(ServerAndConfig *s)
{
    s->srv.stop();
    delete s;
}

static int main2(MxStore& mxs, int argc, char** argv)
{
    ScopeTimer timer;
    MxSources sources(mxs);

    std::vector<ServerAndConfig*> servers;

    {
        DataTree cfgtree(DataTree::TINY);
        if (!doargs(cfgtree, argc, argv, argsCallback, NULL))
            bail("Failed to handle cmdline. Exiting.", "");

        // matrix/storage global config, populate this first
        if(!mxs.apply(cfgtree.subtree("/storage")))
            bail("Invalid matrix config. Exiting.", "");

        // ... then init all servers, but don't start them...
        VarCRef serversRef = cfgtree.subtree("/servers");
        if(serversRef.type() != Var::TYPE_ARRAY)
            bail("Config->servers should be array, but is: ", serversRef ? serversRef.typestr() : "(not-existent)");

        for(size_t i = 0; i < serversRef.size(); ++i)
        {
            VarCRef serv = serversRef.at(i);
            if(serv.type() != Var::TYPE_MAP)
                bail("Entry in Config->servers[] is not map, exiting", "");

            printf("Create new server, index %u\n", (unsigned)i);
            ServerAndConfig *sc = ServerAndConfig::New(serv, mxs);
            if(!sc)
                bail("Invalid config after processing options. Fix your config file(s). Try --help.\nCurrent config:\n",
                    dumpjson(cfgtree.root(), true).c_str()
                );

            servers.push_back(sc);
        }

        // This may take a while to populate the initial 3pid tree
        if(!sources.initConfig(cfgtree.subtree("/sources"), cfgtree.subtree("/env")))
            bail("Invalid sources config. Exiting.", "");

    }

    const bool loaded = mxs.load();

    // need data to operate on. this takes a while so if we already have data, start up with those
    if(loaded)
        printf("Loaded cached data; doing fast startup by skippiping pre-start populate\n");
    else
        sources.initPopulate();

    if(!sExit)
    {
        mxs.rotateHashPepper();

        WebServer::StaticInit();

        for(size_t i = 0; i < servers.size(); ++i)
            if(!servers[i]->start())
                bail("Failed to start a server component, exiting", "");

        printf("Ready; all servers up after %u ms\n", (unsigned)timer.ms());

        if(loaded)
            sources.initPopulate();

        while (!s_quit)
            sleepMS(200);

        // parallel shutdown to save time
        {
            std::vector<std::future<void> > tmp;
            for (size_t i = 0; i < servers.size(); ++i)
                tmp.push_back(std::move(std::async(stopAndDelete, servers[i])));
            servers.clear();
        }

        WebServer::StaticShutdown();
    }

    return 0;
}

int main(int argc, char** argv)
{
    hash_testall();
    srand(unsigned(time(NULL)));
    handlesigs(sigquit);

    MxStore mxs;
    int ret = main2(mxs, argc, argv);

    mxs.save();
    printf("Exiting.\n");
    return ret;
}
