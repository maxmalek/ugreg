#include <stdlib.h>
#include <atomic>
#include <thread>
#include <map>
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

static void init(int argc, char** argv, ServerConfig& cfg, std::vector<SISClient*>& clients)
{
    handlesigs(sigquit);

    DataTree cfgtree;
    if (!doargs(cfgtree, argc, argv))
        bail("Failed to handle cmdline. Exiting.", "");

    
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
            const char* name = cfgtree.getS(it.key());
            VarCRef xhost = VarCRef(cfgtree, &it.value()).lookup("host");
            VarCRef xtype = VarCRef(cfgtree, &it.value()).lookup("type");
            VarCRef xport = VarCRef(cfgtree, &it.value()).lookup("port");
            const char* host = xhost ? xhost.asCString() : NULL;
            const char* type = xtype ? xtype.asCString() : NULL;
            unsigned port = unsigned(xport && xport.asUint() ? *xport.asUint() : 23);
            printf("Device[%s]: '%s' = %s:%u\n", type, name, host, port);
            if (host && *host && type && *type && name && *name && port)
            {
                SISClientConfig scc;
                scc.name = name;
                scc.host = host;
                scc.type = type;
                scc.port = port;
                clients.push_back(new SISClient(scc));
            }
            else
                printf("Invalid entry, skipping\n");
        }
    }
}

typedef std::map<SISSocket, SISClient*> ClientMap;

int main(int argc, char** argv)
{
    ServerConfig cfg;
    std::vector<SISClient*> clients;
    ClientMap sock2cli;
    SISSocketSet socketset;

    init(argc, argv, cfg, clients);

    WebServer::StaticInit();
    WebServer srv;
    if (!srv.start(cfg))
        bail("Failed to start server!", "");

    puts("Ready!");

    u64 lasttime = timeNowMS();
    while (!s_quit)
    {
        size_t changed = 0;
        SISSocketSet::SocketAndStatus *ss = socketset.update(&changed, 1000);
        for(size_t i = 0; i < changed; ++i)
        {
            unsigned flags = ss[i].flags;
            SISSocket s = ss[i].socket;
            SISClient *c = sock2cli[s];
            assert(c);

            if(flags & SISSocketSet::CANREAD)
                c->updateIncoming();
            if(flags & SISSocketSet::CANDISCARD)
            {
                c->disconnect();
                sock2cli.erase(s);
            }
        }

        u64 now = timeNowMS();
        u64 dt = now - lasttime;
        lasttime = now;

        for(size_t i = 0; i < clients.size(); ++i)
        {
            SISClient* c = clients[i];
            if(!c->isConnected())
            {
                SISSocket s = c->connect();
                if(s != sissocket_invalid())
                    sock2cli[s] = c;
            }
            clients[i]->updateTimer(dt);
        }
    }

    srv.stop();

    for (size_t i = 0; i < clients.size(); ++i)
        delete clients[i];

    WebServer::StaticShutdown();
    return 0;
}
