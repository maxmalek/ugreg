#include <stdio.h>
#include <sstream>
#include <atomic>
#include <assert.h>
#include <signal.h>

#include "webserver.h"
#include "config.h"
#include "handler_get.h"

#include <Windows.h> // FIXME: kill this shit

#ifndef SIGQUIT
#define SIGQUIT 3
#endif

std::atomic<bool> s_quit;

static void handlesigs(void (*f)(int))
{
    if(!f)
        f = SIG_DFL;
    signal(SIGBREAK, f);
    signal(SIGQUIT, f);
    signal(SIGTERM, f);
    signal(SIGINT, f);
}

static void sigquit(int)
{
    s_quit = true;
    handlesigs(NULL);
}

static int server(const ServerConfig& cfg)
{
    WebServer::StaticInit();

    WebServer srv;
    if (!srv.start(cfg))
        return 1;

    // FIXME: this is ugly
    TreeHandler htree(4); // strlen("/get")
    srv.registerHandler("/get", htree.Handler, &htree);

    while(!s_quit)
        Sleep(100);

    printf("Stopping server...");
    srv.stop();

    WebServer::StaticShutdown();
    return 0;
}

int main(int argc, char** argv)
{
    handlesigs(sigquit);

    ServerConfig cfg;

    int ret = server(cfg);

    return ret;
}
