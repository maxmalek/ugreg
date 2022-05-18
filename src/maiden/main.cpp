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
    mg_write(conn, ver, sizeof(ver) - 1); // don't include trailing \0
    return 200;
}

int main(int argc, char** argv)
{
    srand(unsigned(time(NULL)));
    handlesigs(sigquit);

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
    }

    MxStore mxs;

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
    mxs.save();

    return 0;
}
