#include <stdio.h>
#include "config.h"
#include "util.h"
#include "variant.h"

// make the default values sane for testing and not a security issue.
// We're not MongoDB.
ServerConfig::ServerConfig()
    : listen_threads(0)
    , expose_debug_apis(false)
    , mimetype("text/json; charset=utf-8")
{
    Listen def { "127.0.0.1", 8080, false };
    listen.push_back(def);
    reply_cache.rows = 0;
    reply_cache.columns = 0;
    reply_cache.maxtime = 0;
}

bool ServerConfig::apply(const VarCRef& root)
{
    listen.clear();

    if(!root)
    {
        logerror("C: ERROR: no root node present -- the config tree is empty?!");
        return false;
    }

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
                    listen.push_back(lis);
                }
            }

    if(VarCRef xcert = root.lookup("cert"))
        if(const char *pcert = xcert.asCString())
            cert = pcert;

    VarCRef xdebugapi = root.lookup("expose_debug_apis");
    expose_debug_apis = xdebugapi && xdebugapi.asBool();

    if(VarCRef xmimetype = root.lookup("mimetype"))
        if(const char *mime = xmimetype.asCString())
            mimetype = mime;

    if(VarCRef xthreads = root.lookup("listen_threads"))
        if(const u64 *pth = xthreads.asUint())
            listen_threads = (unsigned)*pth;

    if(VarCRef xc = root.lookup("reply_cache"))
    {
        if(VarCRef x = xc.lookup("rows"))
            if(const u64 *p = x.asUint())
                reply_cache.rows = (unsigned)*p;

        if (VarCRef x = xc.lookup("columns"))
            if (const u64* p = x.asUint())
                reply_cache.columns = (unsigned)*p;

        if (VarCRef x = xc.lookup("maxtime"))
            if(!strToDurationMS_Safe(&reply_cache.maxtime, x.asCString()))
            {
                logerror("C: ERROR: config.reply_cache.maxtime must be duration value");
                return false;
            }
    }

    if(reply_cache.rows && reply_cache.columns)
        logdebug("C: Using reply cache of (%u x %u) entries, maxtime = %" PRIu64 " ms",
            reply_cache.rows, reply_cache.columns, reply_cache.maxtime);
    else
        logdebug("C: Reply cache disabled");

    if (!listen_threads)
    {
        unsigned c = getNumCPUCores();
        listen_threads = 2 * c;
        if (listen_threads < 5)
            listen_threads = 5;
        logdebug("C: config.listen_threads is 0 or not set, autodetected %u cores --> %u threads",
            c, listen_threads);
    }

    return !listen.empty();
}
