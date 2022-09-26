#include "mxservices.h"
#include "webserver.h"
#include "civetweb/civetweb.h"
#include "json_out.h"
#include "mxstore.h"
#include <algorithm>
#include "scopetimer.h"


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
    if(VarCRef xfields = cfg.lookup("fields"))
    {
        if(const Var::Map *m = xfields.v->map())
        {
            for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
            {
                PoolStr ps = xfields.mem->getSL(it.key());
                const Var& val = it.value();
                if(ps.s)
                {
                    MxStore::SearchConfig::Field &fcfg = searchcfg.fields[ps.s];
                    if(val.asBool())
                    {
                        // use defaults, nothing else to do
                    }
                    else if(const Var::Map *fm = val.map())
                    {
                        VarCRef f(xfields.mem, &val);
                        VarCRef xfuzzy = f.lookup("fuzzy");
                        fcfg.fuzzy = xfuzzy && xfuzzy.asBool();
                    }
                    else
                    {
                        printf("search->fields->%s has unhandled type, ignoring\n", ps.s);
                    }
                }
                else
                {
                    printf("search->fields has non-string element\n");
                }
            }
        }
        else
        {
            printf("search->fields is present but not map, ignoring\n");
        }
    }

    if(VarCRef xurl = cfg.lookup("avatar_url"))
        if(const char *url = xurl.asCString())
            searchcfg.avatar_url = url;

    if (VarCRef xdn = cfg.lookup("displayname"))
        if (const char* dn = xdn.asCString())
            searchcfg.displaynameField = dn;

    if(VarCRef xmaxsize = cfg.lookup("maxsize"))
        if(const u64 *pmaxsize = xmaxsize.asUint())
            searchcfg.maxsize = size_t(*pmaxsize);

    printf("MxSearchHandler: maxsize = %u\n", (unsigned)searchcfg.maxsize);
    printf("MxSearchHandler: avatar_url = %s\n", searchcfg.avatar_url.c_str());
    printf("MxSearchHandler: displayname = %s\n", searchcfg.displaynameField.c_str());
    printf("MxSearchHandler: searching %u fields:\n", (unsigned)searchcfg.fields.size());
    for(MxStore::SearchConfig::Fields::iterator it = searchcfg.fields.begin(); it != searchcfg.fields.end(); ++it)
        printf(" + %s [fuzzy = %u]\n", it->first.c_str(), it->second.fuzzy);
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
                if(rd > 0)
                {
                    size_t limit = 10;
                    const char *term = NULL;

                    if(VarCRef xlimit = vars.root().lookup("limit"))
                        if(const u64 *plimit = xlimit.asUint())
                            limit = size_t(*plimit);

                    if(VarCRef xterm = vars.root().lookup("search_term"))
                        term = xterm.asCString();

                    if(term)
                    {
                        std::vector<MxStore::SearchResult> results;
                        _store.search(results, searchcfg, term);

                        std::sort(results.begin(), results.end());

                        size_t N = results.size();
                        if(limit && limit < N)
                            N = limit;

                        // re-use the same mem to generate output
                        // 'term' var will dangle once this is called
                        Var::Map *m = vars.root().v->map();
                        assert(m);
                        m->clear(vars);

                        vars.root()["limited"] = N < results.size();
                        VarRef ra = vars.root()["results"].makeArray(N);

                        const bool useAvatar = !searchcfg.avatar_url.empty();
                        for(size_t i = 0; i < N; ++i)
                        {
                            VarRef dst = ra.at(i).makeMap();
                            const MxStore::SearchResult& r = results[i];
                            if(useAvatar)
                                dst["avatar_url"] = searchcfg.avatar_url.c_str();
                            if(!r.displayname.empty())
                                dst["display_name"] = r.displayname.c_str();
                            dst["user_id"] = r.str.c_str();

                        }
                        writeJson(dst, vars.root(), false);
                        return 0;
                    }
                }
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
