#include "mxhttprequest.h"
#include <sstream>
#include <civetweb/civetweb.h>
#include "request.h"

mg_connection* mxConnectTo(const URLTarget& target, char *errbuf, size_t errbufsz)
{
    mg_client_options opt = {};
    opt.host = target.host.c_str();
    opt.host_name = opt.host;
    opt.port = target.port;

    if (mg_connection* c = target.ssl
        ? mg_connect_client_secure(&opt, errbuf, errbufsz)
        : mg_connect_client(opt.host, opt.port, target.ssl, errbuf, errbufsz))
    {
        printf("Connected to %s:%u\n", target.host.c_str(), target.port);
        return c;
    }
    else
        printf("Failed %s:%u: %s\n", target.host.c_str(), target.port, errbuf);
    return NULL;
}

static void formatGet(std::ostringstream& os, const URLTarget& target)
{
    os << "GET " << target.path << " HTTP/1.1\r\n"
        //os << "GET / HTTP/1.1\r\n"
        << "Host: " << target.host << "\r\n"
        << "Connection: close\r\n"
        << "\r\n";
}

static void formatPost(std::ostringstream& os, const URLTarget& target, const VarCRef& data)
{
    // TODO: Do we need to encode any data into the POST request? application/x-www-form-urlencoded?
    os << "POST " << target.path << " HTTP/1.1\r\n"
        //os << "GET / HTTP/1.1\r\n"
        << "Host: " << target.host << "\r\n"
        << "Connection: close\r\n"
        << "\r\n";

    assert(!data); // FIXME
}

MxGetJsonResult mxRequestJson(RequestType rqt, VarRef dst, const URLTarget& target, const VarCRef& data, int timeoutMS, size_t maxsize)
{
    char errbuf[1024] = { 0 };
    MxGetJsonResult ret = MXGJ_CONNECT_FAILED;

    if (mg_connection* c = mxConnectTo(target, errbuf, sizeof(errbuf)))
    {
        std::ostringstream os;
        switch(rqt)
        {
            default: assert(false);
            case RQ_GET: formatGet(os, target); break;
            case RQ_POST: formatPost(os, target, data); break; // TODO: data?
        }
       
        std::string request = os.str();
        mg_write(c, request.c_str(), request.size());

        int st = mg_get_response(c, errbuf, sizeof(errbuf), (int)timeoutMS);
        if(st >= 0)
        {
            const mg_response_info *info = mg_get_response_info(c);
            // FIXME: handle redirects here
            printf("mxGetJson: %u (%s), len = %d\n", info->status_code, info->status_text, (int)info->content_length);
            int r = Request::ReadJsonBodyVars(dst, c, true, false, maxsize);
            printf("... JSON parse returned %d\n", r);
            ret = r < 0 ? MXGJ_PARSE_ERROR : MXGJ_OK;
        }
        mg_close_connection(c);
    }

    if(ret != MXGJ_OK)
        printf("mxGetJson error (%d): %s\n", ret, errbuf);

    return ret;
}
