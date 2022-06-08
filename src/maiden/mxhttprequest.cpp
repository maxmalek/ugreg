#include "mxhttprequest.h"
#include <sstream>
#include <civetweb/civetweb.h>
#include "request.h"

MxGetJsonResult mxGetJson(VarRef dst, const char * host, unsigned port, const char * res, int timeoutMS, size_t maxsize)
{
    char errbuf[1024] = {0};
    std::ostringstream os;
    os << "GET " << res << " HTTP/1.1\r\n"
        //os << "GET / HTTP/1.1\r\n"
        << "Host: " << host << "\r\n"
        << "Connection: close\r\n"
        << "\r\n";

    struct mg_client_options opt = { 0 };
    opt.host = host;
    opt.port = port;
    opt.client_cert = NULL;
    opt.server_cert = NULL;
    opt.host_name = opt.host;

    MxGetJsonResult ret = MXGJ_CONNECT_FAILED;

    printf("mxGetJson: %s:%u GET %s\n", host, port, res);
    if (mg_connection* c = mg_connect_client_secure(&opt, errbuf, sizeof(errbuf)))
    {
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
