#include <stdlib.h>
#include <atomic>
#include <thread>
#include "datatree.h"
#include "serverutil.h"
#include "sisclient.h"
#include "webserver.h"
#include "config.h"
#include "json_out.h"


std::atomic<bool> s_quit;

static void sigquit(int)
{
    s_quit = true;
    handlesigs(NULL);
}

int main(int argc, char** argv)
{
    handlesigs(sigquit);

    DataTree cfgtree;
    if (!doargs(cfgtree, argc, argv))
        bail("Failed to handle cmdline. Exiting.", "");

    ServerConfig cfg;
    if (!cfg.apply(cfgtree.subtree("/config")))
    {
        bail("Invalid config after processing options. Fix your config file(s).\nCurrent config:\n",
            dumpjson(cfgtree.root(), true).c_str()
        );
    }

    if (VarCRef devices = cfgtree.subtreeConst("/devices"))
    {
        if (devices.type() != Var::TYPE_MAP)
            bail("devices is not map", "");

        const Var::Map* m = devices.v->map();
        for (Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
        {
            const char *name = cfgtree.getS(it.key());
            VarCRef xhost = VarCRef(cfgtree, &it.value()).lookup("host");
            VarCRef xtype = VarCRef(cfgtree, &it.value()).lookup("type");
            VarCRef xport = VarCRef(cfgtree, &it.value()).lookup("port");
            const char* host = xhost ? xhost.asCString() : NULL;
            const char* type = xtype ? xtype.asCString() : NULL;
            unsigned port = xport && xport.asUint() ? *xport.asUint() : 23;
            printf("Device[%s]: '%s' = %s:%u\n", type, name, host, port);
            if (host && *host && type && *type && name && *name && port)
            {
                SISClientConfig scc;
                scc.name = name;
                scc.host = host;
                scc.type = type;
                scc.port = port;
            }
            else
                printf("Invalid entry, skipping\n");
        }
    }

    WebServer::StaticInit();
    WebServer srv;
    if (!srv.start(cfg))
        bail("Failed to start server!", "");


    return 0;
}
