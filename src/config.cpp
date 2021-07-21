#include "config.h"

// make the default values sane for testing and not a security issue.
// We're not MongoDB.
ServerConfig::ServerConfig()
    : listen_threads(0)
{
    Listen def { "127.0.0.1", 8080, false };
    listen.emplace_back(def);
}

void ServerConfig::clear()
{
    listen.clear();
}

void ServerConfig::add(VarCRef root)
{
    if(VarCRef L = root.lookup("listen"))
        for(size_t i = 0; i < L.size(); ++i)
            if(VarCRef e = L.at(i))
            {
                VarCRef xhost = e.lookup("host");
                VarCRef xport = e.lookup("port");
                VarCRef xssl = e.lookup("port");
                const char *host = xhost && xhost.asCString() ? xhost.asCString() : "";
                bool ssl = xssl && xssl.asBool();
                if(xport && xport.asUint())
                {
                    Listen lis { host, unsigned(*xport.asUint()), ssl };
                    listen.emplace_back(lis);
                }
            }
}

bool ServerConfig::valid() const
{
    return !listen.empty();
}
