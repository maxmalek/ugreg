#include "mxservices.h"
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

MxWellknownHandler::MxWellknownHandler(VarCRef cfg)
    : RequestHandler(WellknownPrefix, MimeType)
{
    if(const Var::Map *m = cfg.v->map())
        for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
        {
            const char *key = cfg.mem->getS(it.key());
            VarCRef value(cfg.mem, &it.value());
            data[key] = dumpjson(value);
        }
}

int MxWellknownHandler::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    const std::string *resp = NULL;

    if(rq.type == RQ_GET)
    {
        const char *tail = rq.query.c_str();
        if(*tail == '/')
        {
            ++tail;
            auto it = data.find(tail);
            if(it != data.end())
                resp = &it->second;
        }
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

    if(VarCRef xmaxsize = cfg.lookup("maxsize"))
        if(const u64 *pmaxsize = xmaxsize.asUint())
            searchcfg.maxsize = size_t(*pmaxsize);

    printf("MxSearchHandler: fuzzy = %d\n", searchcfg.fuzzy);
    printf("MxSearchHandler: maxsize = %u\n", (unsigned)searchcfg.maxsize);
    printf("MxSearchHandler: avatar_url = %s\n", searchcfg.avatar_url.c_str());
    printf("MxSearchHandler: searching in %u media:\n", (unsigned)searchcfg.media.size());
    for(size_t i = 0; i < searchcfg.media.size(); ++i)
        printf("MxSearchHandler: + %s\n", searchcfg.media[i].c_str());
}

int MxSearchHandler::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    if(rq.type == RQ_POST)
    {
        const char *q = rq.query.c_str();
        if(*q == '/')
        {
            ++q;
            const char *tail = strchr(q, '/'); // skip 1 path component
            if(tail && !strcmp(tail, ClientSearchPostfix))
            {
                DataTree vars(DataTree::TINY);
                //int rd = rq.AutoReadVars(vars.root(), conn);
                int rd = rq.ReadJsonBodyVars(vars.root(), conn, true, false, searchcfg.maxsize);

                size_t limit = 10;
                const char *term = "";


                std::vector<MxStore::SearchResult> results;
                _store.search(results, searchcfg, term);
                dst.WriteStr("Search TODO");
                return 0;
            }
        }
    }

    return HANDLER_FALLTHROUGH;
}

MxReverseProxyHandler::MxReverseProxyHandler()
    : RequestHandler("/_matrix", MimeType)
{

}

int MxReverseProxyHandler::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}
