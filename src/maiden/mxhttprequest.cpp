#include "mxhttprequest.h"
#include <sstream>
#include <civetweb/civetweb.h>
#include "request.h"
#include "json_out.h"
#include "jsonstreamwrapper.h"
#include "socketstream.h"
#include "util.h"

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
        logdebug("Connected to %s:%u", target.host.c_str(), target.port);
        return c;
    }
    else
        logerror("Failed %s:%u: %s", target.host.c_str(), target.port, errbuf);
    return NULL;
}

static void formatHeaders(std::ostringstream& os, const VarCRef& hdrs)
{
    if(!hdrs || hdrs.isNull())
        return;
    const Var::Map *m = hdrs.v->map();
    assert(m);

    for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
    {
        const char *key = hdrs.mem->getS(it.key());
        const char *val = it.value().asCString(*hdrs.mem);
        assert(key && val);
        os << key << ": " << val << "\r\n";
    }
}

static void formatGet(std::ostringstream& os, const URLTarget& target, const VarCRef& hdrs)
{
    assert(!target.path.empty());
    assert(!target.host.empty());
    os << "GET " << target.path << " HTTP/1.1\r\n"
        << "Host: " << target.host << "\r\n"
        << "Connection: close\r\n"
        << "Accept: application/json\r\n";
    formatHeaders(os, hdrs);
    os << "\r\n";
}

static void formatPost(std::ostringstream& os, const URLTarget& target, const VarCRef& hdrs)
{
    assert(!target.path.empty());
    assert(!target.host.empty());
    os << "POST " << target.path << " HTTP/1.1\r\n"
        << "Host: " << target.host << "\r\n"
        << "Connection: close\r\n"
        << "Content-Type: application/json\r\n"
        << "Accept: application/json\r\n"
        << "Transfer-Encoding: chunked\r\n";
    formatHeaders(os, hdrs);
    os << "\r\n";
}

static void sendHeaderAndBody(mg_connection *c, const VarCRef& data, const char *hdr, size_t hdrsize)
{
    logdev("HEAD> %s", hdr);

    char buf[8 * 1024];
    SocketWriteStream out(c, buf, sizeof(buf), hdr, hdrsize);
    writeJson(out, data, false);
}

MxGetJsonResult mxRequestJson(RequestType rqt, VarRef dst, const URLTarget& target, const VarCRef& data, const VarCRef& headers, int timeoutMS, size_t maxsize)
{
    char errbuf[1024] = { 0 };
    MxGetJsonResult ret = {MXGJ_CONNECT_FAILED, -1};

    if (mg_connection* c = mxConnectTo(target, errbuf, sizeof(errbuf)))
    {
        std::ostringstream os;
        switch(rqt)
        {
            default: assert(false); [[fallthrough]];
            case RQ_GET:
                assert(!data);
                formatGet(os, target, headers);
                break;
            case RQ_POST:
                formatPost(os, target, headers);
                break; 
        }

        std::string request = os.str();

        sendHeaderAndBody(c, data, request.c_str(), request.length());

        int st = mg_get_response(c, errbuf, sizeof(errbuf), (int)timeoutMS);
        if(st >= 0)
        {
            const mg_response_info *info = mg_get_response_info(c);
            // FIXME: handle redirects here
            logdebug("mxGetJson: %u (%s), len = %d", info->status_code, info->status_text, (int)info->content_length);
            ret.code = MXGJ_HTTP_ERROR;
            int r = 0;
            if(info->status_code == 200)
            {
                r = Request::ReadJsonBodyVars(dst, c, true, false, maxsize);
                logdebug("... JSON parse returned %d", r);
                ret.code = r < 0 ? MXGJ_PARSE_ERROR : MXGJ_OK;
            }
            else
                ret.errmsg = info->status_text;
            ret.httpstatus = info->status_code;
        }
        mg_close_connection(c);
    }

    if(ret.code != MXGJ_OK)
        logerror("mxGetJson error (%d): [%s] %s", ret.code, errbuf, ret.errmsg.c_str());

    return ret;
}

std::string MxGetJsonResult::getErrorMsg() const
{
    if(code == MXGJ_OK)
        return std::string();
    std::ostringstream os;
    os << "mxGetJson error [";
    switch(code)
    {
        case MXGJ_OK: os << "MXGJ_OK"; break;
        case MXGJ_CONNECT_FAILED: os << "MXGJ_CONNECT_FAILED"; break;
        case MXGJ_PARSE_ERROR: os << "MXGJ_PARSE_ERROR"; break;
        case MXGJ_HTTP_ERROR: os << "MXGJ_HTTP_ERROR"; break;
    }
    os << " (" << code << ")] [HTTP status = " << httpstatus << "]";
    if(!errmsg.empty())
        os << ": " << errmsg;
    return os.str();
}
