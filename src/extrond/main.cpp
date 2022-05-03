// extrond main

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
#include "handler_status.h"
#include "handler_ctrl.h"
#include "util.h"

static std::atomic<bool> s_quit;

static u64 idleWaitTime = 100;

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

    VarCRef config = cfgtree.subtree("/config");
    if (cfg.apply(config))
    {
        VarCRef xiwt = config.lookup("idle_wait_time");
        if(xiwt)
            if(!strToDurationMS_Safe(&idleWaitTime, xiwt.asCString()))
                bail("idle_wait_time given but not a string or failed to parse as duration", "");
        printf("idle_wait_time = %u ms\n", (unsigned)idleWaitTime);
    }
    else
    {
        bail("Invalid config after processing options. Fix your config file(s).\nCurrent config:\n",
            dumpjson(cfgtree.root(), true).c_str()
        );
    }

    VarCRef dt = cfgtree.subtreeConst("/devicetypes");
    if(!dt || dt.type() != Var::TYPE_MAP)
        bail("devicetypes is not map", "");

    if (VarCRef devices = cfgtree.subtreeConst("/devices"))
    {
        if (devices.type() != Var::TYPE_MAP)
            bail("devices is not map", "");

        const Var::Map* m = devices.v->map();
        for (Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
        {
            const char* name = cfgtree.getS(it.key());
            const VarCRef mycfg(cfgtree, &it.value());

            const VarCRef xtype = mycfg.lookup("type");
            const char* type = xtype ? xtype.asCString() : NULL;
            if(!type)
                bail("Client has no device type: ", name);

            VarCRef devcfg = dt.lookup(type);
            if(!devcfg || devcfg.type() != Var::TYPE_MAP)
                bail("Unknown device type: ", type);

            printf("- %s is device type [%s]\n", name, type);

            SISClient *client = new SISClient(name);
            if(client->configure(mycfg, devcfg))
                clients.push_back(client);
            else
            {
                delete client;
                bail("Client failed to configure, fix the config for ", name);
            }
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

    StatusHandler status(clients, "/status");
    srv.registerHandler(status);

    CtrlHandler ctrl(clients, "/ctrl");
    srv.registerHandler(ctrl);

    puts("Ready!");

    u64 lasttime = timeNowMS();
    u64 timeUntilNext = 0;
    while (!s_quit)
    {
        size_t changed = 0;
        SISSocketSet::SocketAndStatus *ss = socketset.update(&changed, (int)timeUntilNext);
        for(size_t i = 0; i < changed; ++i)
        {
            unsigned flags = ss[i].flags;
            SISSocket s = ss[i].socket;
            ClientMap::iterator ci = sock2cli.find(s);
            SISClient *c = ci != sock2cli.end() ? ci->second : NULL;

            if(c)
            {
                if(flags & SISSocketSet::JUSTCONNECTED)
                    c->delayedConnected();
                if(flags & SISSocketSet::CANREAD)
                    c->updateIncoming();
                if(flags & SISSocketSet::CANDISCARD)
                    c->wasDisconnected(); // socket is closed already
            }

            // can happen that this flag is set but the socket is not in sock2cli
            if (c && (flags & SISSocketSet::CANDISCARD))
                sock2cli.erase(s);
        }

        u64 now = timeNowMS();
        u64 dt = now - lasttime;
        lasttime = now;
        timeUntilNext = idleWaitTime;
        for(size_t i = 0; i < clients.size(); ++i)
        {
            SISClient* c = clients[i];

            // Don't spam-connect in error state
            if(c->getState() == SISClient::DISCONNECTED)
            {
                SISSocket s = c->connect();
                if(s != sissocket_invalid())
                {
                    sock2cli[s] = c;
                    socketset.add(s);
                }
            }

            u64 next = clients[i]->updateTimer(now, dt);
            if(next)
                timeUntilNext = std::min(timeUntilNext, next);
        }
        //printf("timeUntilNext = %u, now = %zu\n", (unsigned)timeUntilNext, now);
    }

    srv.stop();

    for (size_t i = 0; i < clients.size(); ++i)
        delete clients[i];

    WebServer::StaticShutdown();
    return 0;
}
