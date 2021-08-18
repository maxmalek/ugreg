#include <stdio.h>
#include "config.h"
#include "util.h"

// make the default values sane for testing and not a security issue.
// We're not MongoDB.
ServerConfig::ServerConfig()
    : listen_threads(0)
    , expose_debug_apis(false)
{
    Listen def { "127.0.0.1", 8080, false };
    listen.emplace_back(def);
    reply_cache.rows = 0;
    reply_cache.columns = 0;
    reply_cache.maxtime = 0;
}

bool ServerConfig::apply(VarCRef root)
{
    listen.clear();

    if(!root)
        return false;

    if(VarCRef L = root.lookup("listen"))
        for(size_t i = 0; i < L.size(); ++i)
            if(VarCRef e = L.at(i))
            {
                VarCRef xhost = e.lookup("host");
                VarCRef xport = e.lookup("port");
                VarCRef xssl = e.lookup("ssl");
                const char *host = xhost && xhost.asCString() ? xhost.asCString() : "";
                bool ssl = xssl && xssl.asBool();
                if(xport && xport.asUint())
                {
                    Listen lis { host, unsigned(*xport.asUint()), ssl };
                    listen.emplace_back(lis);
                }
            }

    VarCRef xdebugapi = root.lookup("expose_debug_apis");
    expose_debug_apis = xdebugapi && xdebugapi.asBool();

    if(VarCRef xthreads = root.lookup("listen_threads"))
        if(const u64 *pth = xthreads.asUint())
            listen_threads = *pth;

    if(VarCRef xc = root.lookup("reply_cache"))
    {
        if(VarCRef x = xc.lookup("rows"))
            if(const u64 *p = x.asUint())
                reply_cache.rows = *p;

        if (VarCRef x = xc.lookup("columns"))
            if (const u64* p = x.asUint())
                reply_cache.columns = *p;

        if (VarCRef x = xc.lookup("maxtime"))
            if(!strToDurationMS_Safe(&reply_cache.maxtime, x.asCString()))
            {
                printf("C: ERROR: config.reply_cache.maxtime must be duration value\n");
                return false;
            }
    }

    if(reply_cache.rows && reply_cache.columns)
        printf("C: Using reply cache of (%u x %u) entries, maxtime = %" PRIu64 " ms\n",
            reply_cache.rows, reply_cache.columns, reply_cache.maxtime);
    else
        printf("C: Reply cache disabled\n");

    if (!listen_threads)
    {
        unsigned c = getNumCPUCores();
        listen_threads = 2 * c;
        if (listen_threads < 5)
            listen_threads = 5;
        printf("C: config.listen_threads is 0 or not set, autodetected %u cores --> %u threads\n",
            c, listen_threads);
    }

    return !listen.empty();
}
