#include <stdio.h>
#include <sstream>
#include <atomic>

#include "variant.h"
#include "webserver.h"
#include "config.h"


#include <assert.h>
#include "json_in.h"

#include "civetweb/civetweb.h"
#include "handler_get.h"

#include <Windows.h> // FIXME: kill this shit


static int server(const ServerConfig& cfg)
{
    WebServer::StaticInit();

    WebServer srv;
    if (!srv.start(cfg))
        return 1;

    // FIXME: this is ugly
    TreeHandler htree(4); // strlen("/get")
    srv.registerHandler("/get", htree.Handler, &htree);


    for(;;)
        Sleep(1000);

    srv.stop();

    WebServer::StaticShutdown();
    return 0;
}

int main(int argc, char** argv)
{
    ServerConfig cfg;

    int ret = server(cfg);

    return ret;
}
