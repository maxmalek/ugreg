#if defined(_DEBUG) && defined(_MSC_VER)
    #define _CRTDBG_MAP_ALLOC
    #include <stdlib.h>
    #include <crtdbg.h>
#endif

#include <stdio.h>
#include <sstream>
#include <atomic>

#include "variant.h"
#include "webserver.h"
#include "config.h"

#include <Windows.h>
#include <assert.h>
#include "json_in.h"
#include "json_out.h"
#include "accessor.h"

#include "civetweb/civetweb.h"
#include "handler_get.h"

#include "subproc.h"


/*

endpoints?:
/get/... ->
/help
/status
/schema[/xxx] -> output defined schema(ta)

?params:

fmt=json
fmt=msgpack
fmt=xml

pretty=1


possible features:

- write state to disk (on exit, periodically, SIGUSR1, ...)
- start with init.json to populate initial tree
- query stuff using json pointers
*/




std::atomic<int> s_quit;


#ifdef _DEBUG
int handler_debug(struct mg_connection* conn, void* cbdata)
{
    const mg_request_info* info = mg_get_request_info(conn);

    std::ostringstream os;
    os << info->local_uri;
    if (info->query_string)
        os << "\n\n" << info->query_string;

    std::string out = os.str();
    puts(out.c_str());
    mg_send_http_ok(conn, "text/plain", out.size());
    mg_write(conn, out.c_str(), out.size());

    return 200; /* HTTP state 200 = OK */
}

int handler_quit(struct mg_connection* conn, void* cbdata)
{
    s_quit = 1;
    return 204;
}
#endif

static int server()
{
    WebServer::StaticInit();


    ServerConfig cfg;
    WebServer srv;
    if (!srv.start(cfg))
        return 1;

#ifdef _DEBUG
    srv.registerHandler("/debug", handler_debug, NULL);
    srv.registerHandler("/debug/quit", handler_quit, NULL);
#endif

    TreeHandler htree(4); // strlen("/get")
    srv.registerHandler("/get", htree.Handler, &htree);

#if defined(_DEBUG) && defined(_MSC_VER)
    {
        _CrtMemState mem;
        _CrtMemCheckpoint(&mem);
        _CrtMemDumpStatistics(&mem);
    }
#endif

    while (!s_quit)
        Sleep(1000);

    srv.stop();

    WebServer::StaticShutdown();
    return 0;
}

int main(int argc, char** argv)
{
#if defined(_DEBUG) && defined(_MSC_VER)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF | _CRTDBG_CHECK_CRT_DF);
#endif
    int ret = server();

#if defined(_DEBUG) && defined(_MSC_VER)
    _CrtDumpMemoryLeaks();
#endif

    return ret;
}
