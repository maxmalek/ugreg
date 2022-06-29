// piggyback main

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
#include <civetweb/civetweb.h>
#include "json_out.h"
#include "handler_fwd.h"


std::atomic<bool> s_quit;

static void sigquit(int)
{
    s_quit = true;
    handlesigs(NULL);
}

int main(int argc, char** argv)
{
    handlesigs(sigquit);

    PiggybackHandler handler;

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

        handler.apply(cfgtree.root().lookup("forward"));
    }

    WebServer::StaticInit();
    WebServer srv;
    if (!srv.start(cfg))
        bail("Failed to start server!", "");

    srv.registerHandler(handler);

    puts("Ready!");

    while (!s_quit)
        sleepMS(200);

    srv.stop();
    WebServer::StaticShutdown();

    return 0;
}
