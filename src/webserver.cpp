#include <string.h>
#include <sstream>
#include <assert.h>
#include "webserver.h"
#include "civetweb/civetweb.h"
#include "config.h"

#include <thread> // TEMP


WebServer::WebServer()
    : _ctx(NULL)
{
}

WebServer::~WebServer()
{
    stop();
}

void WebServer::stop()
{
    if(!_ctx)
        return;
    mg_stop(_ctx);
    _ctx = NULL;
}

void WebServer::registerHandler(const char *entrypoint, mg_request_handler h, void *ud)
{
    assert(_ctx && entrypoint && entrypoint[0] == '/');
    mg_set_request_handler(_ctx, entrypoint, h, ud);
}

bool WebServer::start(const ServerConfig& cfg)
{
    mg_callbacks cb;
    memset(&cb, 0, sizeof(cb));

    std::string listenbuf, threadsbuf;
    {
        std::ostringstream ls;
        for(size_t i = 0; i < cfg.listen.size(); ++i)
        {
            if(i)
                ls << ',';
            const ServerConfig::Listen& lis = cfg.listen[i];
            if(lis.host.length())
                ls << lis.host << ':';
            ls << lis.port;
            if(lis.ssl)
                ls << 's';
        }
        listenbuf = ls.str();
        printf("Listening on %s\n", listenbuf.c_str());
    }
    {
        unsigned threads = cfg.listen_threads;
        if(!threads)
        {
            threads = 2 * std::thread::hardware_concurrency();
            if(threads < 5)
                threads = 5;
        }
        printf("Using %u request worker threads\n", threads);
        threadsbuf = std::to_string(threads);
    }
    

    // via https://github.com/civetweb/civetweb/blob/master/docs/UserManual.md
    // Everything is handled programmatically, so there is no document_root
    // TODO: maybe set linger_timeout_ms to prevent DoS?
    const char* options[] =
    {
        "listening_ports", listenbuf.c_str(),
        "num_threads", threadsbuf.c_str(),
        NULL
    };

    mg_context *ctx = mg_start(&cb, NULL, options);
    if(!ctx)
        return false;

    _ctx = ctx;
    return true;
}

bool WebServer::StaticInit()
{
    return !!mg_init_library(0); // TODO: init TLS etc here?
}

void WebServer::StaticShutdown()
{
    mg_exit_library();
}
