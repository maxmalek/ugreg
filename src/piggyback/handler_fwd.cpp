#include "handler_fwd.h"
#include <sstream>
#include <stdio.h>
#include "civetweb/civetweb.h"
#include "json_out.h"
#include "json_in.h"
#include "jsonstreamwrapper.h"
#include "treefunc.h"
#include "config.h"
#include "responseformat.h"

#define BR "<br />\n"


PiggybackHandler::PiggybackHandler()
    : RequestHandler("/", "")
{
}

bool PiggybackHandler::apply(VarCRef cfg)
{
    const size_t n = cfg.size();
    const Var *a = cfg.v->array();
    if(!a)
        return false;

    dest.clear();
    for(size_t i = 0; i < n; ++i)
    {
        VarCRef u(cfg.mem, &a[i]);
        VarCRef xhost = u.lookup("host");
        VarCRef xport = u.lookup("port");
        VarCRef xfixhost = u.lookup("fixHost");

        if(xhost && xport)
        {
            const char *host = xhost.asCString();
            const unsigned port = xport.asUint() ? *xport.asUint() : 0;
            if(host && *host && port)
            {
                printf("PiggybackHandler: Add %s:%u\n", host, port);
                Destination d { host, port, xfixhost && xfixhost.asBool() };
                dest.push_back(std::move(d));
            }
        }

    }
    return true;
}

PiggybackHandler::~PiggybackHandler()
{
}

struct AutoClose
{
    AutoClose(mg_connection *c) : _c(c) {}
    mg_connection * const _c;
    ~AutoClose() { if(_c) mg_close_connection(_c); }
};

// This is called from many threads at once.
// Avoid anything that changes the tree, and ensure proper read-locking!
int PiggybackHandler::onRequest(BufferedWriteStream& /*dst*/, mg_connection* conn, const Request& rq) const
{
    const mg_request_info *info = mg_get_request_info(conn);

    const char *q = rq.query.c_str();
    printf("<<< %s %s\n", info->request_method, q);

    const Destination *d;
    mg_connection *c = connectToDest(d);
    if(!c)
    {
        mg_send_http_error(conn, 504, "all destinations offline");
        return 504;
    }
    AutoClose ac(c);


    const char *uri = info->local_uri_raw ? info->local_uri_raw : "";
    const char *sep = info->local_uri_raw ? " " : "";

    // Prep header
    std::ostringstream os;
    os << info->request_method << sep << uri << " HTTP/" << info->http_version << "\r\n";

    for(int i = 0; i < info->num_headers; ++i)
    {
        const char *name = info->http_headers[i].name;
        const char *value = info->http_headers[i].value;

        if(d->fixHost && !mg_strcasecmp(name, "host"))
            value = d->host.c_str();

        os << name << ": " << value << "\r\n";
    }
    os << "\r\n";

    std::string hdr = os.str();
    printf(">>> %s", hdr.c_str());
    mg_write(c, hdr.c_str(), hdr.length());

    // forward body (in case of POST)
    for(;;)
    {
        char inbuf[1024];
        int rd = mg_read(conn, inbuf, sizeof(inbuf));
        if(rd > 0)
        {
            int wr = mg_write(c, inbuf, rd);
            if(wr < rd)
            {
                assert(false);
                mg_send_http_error(conn, 502, "stream terminated");
                return 502;
            }

        }
        else if(!rd)
            break;
        else
        {
            assert(false);
            break;
        }
    }

    char errbuf[1024];
    int st = mg_get_response(c, errbuf, sizeof(errbuf), (int)5000);

    if(st < 0)
    {
        printf("mg_get_response ERROR: %s\n", errbuf);
        mg_send_http_error(conn, 500, "response unreadable: %s", errbuf);
        return 500;
    }
    const mg_response_info *ri = mg_get_response_info(c);
    const bool chunked = ri->content_length < 0;

    mg_response_header_start(conn, ri->status_code);

    for(int i = 0; i < ri->num_headers; ++i)
    {
        const char *name = ri->http_headers[i].name;
        const char *value = ri->http_headers[i].value;
        printf(">> %s: %s\n", name, value);
        mg_response_header_add(conn, name, value, -1);
    }
    if(chunked)
        mg_response_header_add(conn, "Transfer-Encoding", "chunked", -1);
    mg_response_header_send(conn);

    // forward response
    for(;;)
    {
        char inbuf[1024];
        int rd = mg_read(c, inbuf, sizeof(inbuf));
        if(rd > 0)
        {
            int wr = chunked ? mg_send_chunk(conn, inbuf, rd) : mg_write(conn, inbuf, rd);
            if(wr < rd)
            {
                assert(false);
                mg_send_http_error(conn, 502, "stream terminated");
                return 502;
            }

        }
        else
            break;
    }
    if(chunked)
        mg_send_chunk(conn, "", 0);

    return 200;
}

mg_connection* PiggybackHandler::connectToDest(const Destination *& dst) const
{
    mg_client_options opt = {};
    for(size_t i = 0; i < dest.size(); ++i)
    {
        opt.host = dest[i].host.c_str();
        opt.host_name = dest[i].host.c_str();
        opt.port = dest[i].port;
        char errbuf[1024];
        if(mg_connection *c = mg_connect_client_secure(&opt, errbuf, sizeof(errbuf)))
        {
            dst = &dest[i];
            printf("Connected to %s:%u\n", dest[i].host.c_str(), dest[i].port);
            return c;
        }
        else
            printf("Failed %s:%u: %s\n", dest[i].host.c_str(), dest[i].port, errbuf);
    }
    dst = NULL;
    return NULL;
}
