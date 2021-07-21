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
}

bool ServerConfig::apply(VarCRef root)
{
    listen.clear();

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

    if (!listen_threads)
    {
        listen_threads = 2 * getNumCPUCores();
        if (listen_threads < 5)
            listen_threads = 5;
    }

    return !listen.empty();
}
