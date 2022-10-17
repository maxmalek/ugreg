#include "mxhttprequest.h"
#include <sstream>
#include <civetweb/civetweb.h>
#include "request.h"
#include "json_out.h"
#include "jsonstreamwrapper.h"
#include "socketstream.h"

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
        << "Host: " << target.host << "\r\n"
        << "Connection: close\r\n"
        << "Accept: application/json\r\n"
        << "\r\n";
}

static void formatPost(std::ostringstream& os, const URLTarget& target)
{
    os << "POST " << target.path << " HTTP/1.1\r\n"
        << "Host: " << target.host << "\r\n"
        << "Connection: close\r\n"
        << "Content-Type: application/json\r\n"
        << "Accept: application/json\r\n"
        << "\r\n";
}

static void sendHeaderAndBody(mg_connection *c, const VarCRef& data, const char *hdr, size_t hdrsize)
{
    //char buf[8 * 1024];
    //SocketWriteStream out(c, buf, sizeof(buf), hdr, hdrsize);
    //writeJson(out, data, false);

    mg_write(c, hdr, hdrsize);

    if(data)
    {
        std::string body = dumpjson(data);
        mg_write(c, body.c_str(), body.length());
    }
}

MxGetJsonResult mxRequestJson(RequestType rqt, VarRef dst, const URLTarget& target, const VarCRef& data, int timeoutMS, size_t maxsize)
{
    char errbuf[1024] = { 0 };
    MxGetJsonResult ret = {MXGJ_CONNECT_FAILED, -1};

    if (mg_connection* c = mxConnectTo(target, errbuf, sizeof(errbuf)))
    {
        std::ostringstream os;
        switch(rqt)
        {
            default: assert(false); [[fallthrough]];
            case RQ_GET: formatGet(os, target); break;
            case RQ_POST: formatPost(os, target); break; // TODO: data?
        }
       
        std::string request = os.str();

        sendHeaderAndBody(c, data, request.c_str(), request.length());

        int st = mg_get_response(c, errbuf, sizeof(errbuf), (int)timeoutMS);
        if(st >= 0)
        {
            const mg_response_info *info = mg_get_response_info(c);
            // FIXME: handle redirects here
            printf("mxGetJson: %u (%s), len = %d\n", info->status_code, info->status_text, (int)info->content_length);
            ret.code = MXGJ_HTTP_ERROR;
            int r = 0;
            if(info->status_code == 200)
            {
                r = Request::ReadJsonBodyVars(dst, c, true, false, maxsize);
                printf("... JSON parse returned %d\n", r);
                ret.code = r < 0 ? MXGJ_PARSE_ERROR : MXGJ_OK;
            }
            else
                ret.errmsg = info->status_text;
            ret.httpstatus = info->status_code;
        }
        mg_close_connection(c);
    }

    if(ret.code != MXGJ_OK)
        printf("mxGetJson error (%d): [%s] %s\n", ret.code, errbuf, ret.errmsg.c_str());

    return ret;
}
