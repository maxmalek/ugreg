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
#include <argh.h>

std::atomic<bool> s_quit;

static void sigquit(int)
{
    s_quit = true;
    handlesigs(NULL);
}

static std::string sDumpFn;
static bool sDump;
static bool sExit;

static const char *usage =
"Usage: ./maiden <switches> config1.json configN.json <switches> ...\n"
"Supported switches:\n"
"-h --help    This help\n"
"--dump       Dump 3pid storage to stdout after populating it\n"
"--dump=file  Dump 3pid storage to file instead\n"
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
    else if (!strncmp(sw, "dump", 4))
    {
        sDump = true;
        sw += 4;
        if (*sw == '=')
            sDumpFn = sw + 1;
        return 1;
    }
    else if (!strcmp(sw, "exit"))
    {
        sExit = true;
        return 1;
    }
    return 0;
}

static void dump(MxStore& mxs)
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

int handler_wellknown(struct mg_connection *conn, void *ud)
{
    const mg_request_info *info = mg_get_request_info(conn);
    printf("wellknown:%s: %s\n", info->request_method, info->local_uri_raw);
    const std::string& s = *(const std::string*)ud;
    mg_send_http_ok(conn, "application/json", s.length());
    mg_response_header_add(conn, "Access-Control-Allow-Origin", "*", 1);
    mg_write(conn, s.c_str(), s.length());
    return 200;
}

class ServerAndConfig
{
    static ServerAndConfig *New(VarCRef json)
    {
        ServerAndConfig *ret = new ServerAndConfig;
        if(ret->cfg.apply(json))
            ret->srv.start(ret->cfg);
        else
        {
            delete ret;
            ret = NULL;
        }
        return ret;
    }
    WebServer srv;
    ServerConfig cfg;

private:
    ServerAndConfig();
};

int main(int argc, char** argv)
{
    srand(unsigned(time(NULL)));
    handlesigs(sigquit);

    MxStore mxs;
    MxSources sources(mxs);
    ServerConfig cfg, wellknownCfg;
    std::string wkServer, wkClient;
    WebServer wksrv;
    {
        DataTree cfgtree;
        if (!doargs(cfgtree, argc, argv, argsCallback, NULL))
            bail("Failed to handle cmdline. Exiting.", "");

        if (!cfg.apply(cfgtree.subtree("/config")))
        {
            bail("Invalid config after processing options. Fix your config file(s). Try --help.\nCurrent config:\n",
                dumpjson(cfgtree.root(), true).c_str()
            );
        }

        if(VarCRef sub = cfgtree.subtree("/wellknown"))
        {
            VarCRef server = sub.lookup("server");
            VarCRef client = sub.lookup("client");

            if(client && server && wellknownCfg.apply(sub))
            {
                wkServer = dumpjson(server);
                wkClient = dumpjson(client);

                if (!wksrv.start(wellknownCfg))
                    bail("Failed to start .well-known server!", "");
                wksrv.registerHandler("/.well-known/matrix/server", handler_wellknown, &wkServer);
                wksrv.registerHandler("/.well-known/matrix/client", handler_wellknown, &wkClient);
            }
            else
                bail("Invalid wellknown config. Exiting.", "");
        }

        if(!mxs.apply(cfgtree.subtree("/matrix")))
            bail("Invalid matrix config. Exiting.", "");

        // This may take a while to populate the initial 3pid tree
        if(!sources.init(cfgtree.subtree("/sources"), cfgtree.subtree("/env")))
            bail("Invalid sources config. Exiting.", "");

        if(sDump)
            dump(mxs);

        if(sExit)
            return 0;
    }

    hash_testall();
    mxs.rotateHashPepper();

    WebServer::StaticInit();
    WebServer srv;
    if (!srv.start(cfg))
        bail("Failed to start server!", "");

    srv.registerHandler("/_matrix/identity/versions", handler_versions, NULL);

    MxidHandler_v2 v2(mxs, "/_matrix/identity/v2");
    srv.registerHandler(v2);

    MxidHandler_v2 v1(mxs, "/_matrix/identity/api/v1");
    srv.registerHandler(v1);

    puts("Ready!");

    while (!s_quit)
        sleepMS(200);

    wksrv.stop();
    srv.stop();

    WebServer::StaticShutdown();
    //mxs.save();

    return 0;
}
