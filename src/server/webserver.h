#pragma once

#include <string>
#include "request.h"
#include "cachetable.h"

struct ServerConfig;
struct mg_context;
typedef int (*mg_request_handler)(struct mg_connection* conn, void* cbdata);

class RequestHandler
{
public:
    typedef CacheTable<Request, const StoredReply> Cache;

    RequestHandler(const char *prefix);
    virtual ~RequestHandler();

    static int Handler(struct mg_connection* conn, void* self);
    static void SendDefaultChunkedOK(mg_connection* conn);

    const char* prefix() const { return myPrefix.c_str(); }

    void setupCache(u32 rows, u32 cols, u64 maxtime);

    int _onRequest(mg_connection* conn) const;

    // Derived classes override this to serve the request.
    // Possibly called by many threads at once.
    virtual int onRequest(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const = 0;

    int onRequest_deflate(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const;

    void clearCache();

private:
    const std::string myPrefix;
    mutable Cache _cache;
    u64 maxcachetime;

};

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

    void registerHandler(const RequestHandler& h);

private:
    mg_context *_ctx;
};
