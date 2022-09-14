#include "mxservices.h"
#include <map>
#include <string>
#include "webserver.h"
#include "civetweb/civetweb.h"
#include "json_out.h"
#include "mxstore.h"

static const char *MimeType = "application/json";
static const char  WellknownPrefix[] = "/.well-known/matrix";

// TODO: see https://github.com/civetweb/civetweb/blob/master/include/civetweb.h#L489
static const char  ClientPrefix[] = "/_matrix/client";
static const char  ClientSearchPostfix[] = "/user_directory/search";

// The search endpoint changed in v0.4.0 of the matrix spec; we support any version tag in between
//static const char SearchPrefix_v04[] = "/_matrix/client/r0/user_directory/search";
//static const char SearchPrefix_v3[] = "/_matrix/client/v3/user_directory/search";

MxWellknownHandler::MxWellknownHandler(VarCRef data)
    : RequestHandler(WellknownPrefix, MimeType)
{
    VarCRef serverRef = data.lookup("server");
    VarCRef clientRef = data.lookup("client");
    if(clientRef)
        client = dumpjson(clientRef);
    if(serverRef)
        server = dumpjson(serverRef);
}

int MxWellknownHandler::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    const std::string *resp = NULL;

    if(rq.type == RQ_GET)
    {
        const char *tail = rq.query.c_str() + Countof(WellknownPrefix) - 1;

        if(!strcmp(tail, "client"))
            resp = &client;
        else if(!strcmp(tail, "server"))
            resp = &server;
    }

    if(!resp)
    {
        mg_send_http_error(conn, 404, "");
        return 404;
    }

    const mg_request_info *info = mg_get_request_info(conn);
    printf("wellknown:%s: %s\n", info->request_method, info->local_uri_raw);
    mg_send_http_ok(conn, "application/json", resp->length());
    mg_response_header_add(conn, "Access-Control-Allow-Origin", "*", 1);
    mg_write(conn, resp->c_str(), resp->length());
    return 200;
}

MxSearchHandler::MxSearchHandler(MxStore& store, VarCRef cfg)
    : RequestHandler(ClientPrefix, MimeType), _store(store)
{
    // the default
    searchcfg.media.push_back("email");

    if(VarCRef xmedia = cfg.lookup("media"))
    {
        const Var *a = xmedia.v->array();
        if(a)
        {
            searchcfg.media.clear();
            for(size_t i = 0; i < xmedia.size(); ++i)
            {
                PoolStr ps = xmedia.at(i).asString();
                if(ps.s)
                {
                    searchcfg.media.push_back(ps.s);
                }
                else
                {
                    printf("search->media has non-string element\n");
                }
            }
        }
        else
        {
            printf("search->media is present but not array, ignoring\n");
        }
    }

    VarCRef xfuzzy = cfg.lookup("fuzzy");
    searchcfg.fuzzy = xfuzzy && xfuzzy.asBool();

    if(VarCRef xurl = cfg.lookup("avatar_url"))
        if(const char *url = xurl.asCString())
            searchcfg.avatar_url = url;

    printf("MxSearchHandler: fuzzy = %d\n", searchcfg.fuzzy);
    printf("MxSearchHandler: avatar_url = %s\n", searchcfg.avatar_url.c_str());
    printf("MxSearchHandler: searching in %u media:\n", (unsigned)searchcfg.media.size());
    for(size_t i = 0; i < searchcfg.media.size(); ++i)
        printf("MxSearchHandler: + %s\n", searchcfg.media[i].c_str());
}

int MxSearchHandler::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    const char *q = rq.query.c_str();
    const char *ver = q + Countof(ClientPrefix) - 1;
    if(*ver == '/')
    {
        const char *tail = strchr(ver, '/');
        if(!strcmp(tail, ClientSearchPostfix))
        {
            DataTree::LockedRoot lut = _store.get3pidRoot();
            dst.WriteStr("Search TODO");
            return 200;
        }
    }

    return 404;
}

MxReverseProxyHandler::MxReverseProxyHandler()
    : RequestHandler("/_matrix", MimeType)
{

}

int MxReverseProxyHandler::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}
