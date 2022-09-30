#include "webstuff.h"
#include "variant.h"
#include <civetweb/civetweb.h>
#include <string.h>
#include <ctype.h>

bool URLTarget::load(const VarCRef& ref)
{
    if (!ref)
        return false;

    VarCRef xhost = ref.lookup("host");
    VarCRef xport = ref.lookup("port");
    VarCRef xssl = ref.lookup("ssl");
    const char* host = xhost ? xhost.asCString() : NULL;
    if(!host)
        return false;
    bool ssl = xssl && xssl.asBool();
    const u64 *pport = xport ? xport.asUint() : NULL;
    if(!pport)
        return false;
    const unsigned port = pport ? (unsigned)*pport : NULL;
    if(!port)
        return false;

    this->host = host;
    this->port = port;
    this->ssl = ssl;
    return true;
}

bool URLTarget::parse(const char* url, unsigned defaultport /* = 0 */)
{
    unsigned port = 0;
    bool http = false, https = false, ssl = false;

    if(!strnicmp(url, "http://", 7))
    {
        http = true;
        url += 7;
    }
    else if(!strnicmp(url, "https://", 8))
    {
        https = true;
        ssl = true;
        url += 8;
    }
    else
    {
        const char *p = url;
        while(*p && isalpha(*p))
            ++p;
        if(p[0] == ':' && p[1] == '/' && p[2] == '/')
            url = p + 3;
    }

    const char * const beginhost = url;
    const char * endhost = NULL;
    const char *colon = strchr(url, ':');
    const char *slash = strchr(url, '/');
    const char *path = NULL;
    if(colon && (!slash || colon+1 < slash))
    {
        endhost = colon;
        char *end = NULL;
        port = strtol(colon + 1, &end, 10);
        if(end)
            path = end;
    }
    if(slash)
    {
        if(!endhost)
            endhost = slash;
        path = slash;
    }

    if(!port)
    {
        if(http)
            port = 80;
        else if(https)
            port = 443;
        else
            port = defaultport;
    }

    if(!port)
        return false;

    if(port == 443)
        ssl = true;

    this->ssl = ssl;
    this->port = port;
    if(!endhost)
        this->host = beginhost;
    else
        this->host.assign(beginhost, endhost - beginhost);
    if(path)
        this->path = path;

    return true;
}
