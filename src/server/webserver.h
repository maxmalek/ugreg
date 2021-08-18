#pragma once

struct ServerConfig;
struct mg_context;
typedef int (*mg_request_handler)(struct mg_connection* conn, void* cbdata);

class WebServer
{
public:
    static bool StaticInit();
    static void StaticShutdown();

    WebServer();
    ~WebServer();
    bool start(const ServerConfig& cfg);
    void stop();

    // pass h == NULL to unregister
    void registerHandler(const char *entrypoint, mg_request_handler h, void *ud);

private:
    mg_context *_ctx;
};
