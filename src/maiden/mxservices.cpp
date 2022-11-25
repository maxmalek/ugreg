#include "mxservices.h"
#include "webserver.h"
#include "civetweb/civetweb.h"
#include "json_out.h"
#include "mxstore.h"
#include <algorithm>
#include "scopetimer.h"
#include "webstuff.h"
#include "mxhttprequest.h"
#include <future>
#include "util.h"
#include "mxsearch.h"
#include "strmatch.h"
#include "mxsources.h"

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

MxSearchHandler::MxSearchHandler(MxStore& store, VarCRef cfg, MxSources& sources)
    : MxReverseProxyHandler(cfg), _store(store), search(searchcfg)
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
                    MxSearchConfig::Field &fcfg = searchcfg.fields[ps.s];
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

    if (VarCRef xproxy = cfg.lookup("reverseproxy"))
        reverseproxy = xproxy && xproxy.asBool();

    if (VarCRef xproxy = cfg.lookup("check_homeserver"))
        checkHS = xproxy && xproxy.asBool();

    if (VarCRef xproxy = cfg.lookup("ask_homeserver"))
        askHS = xproxy && xproxy.asBool();

    hsTimeout = -1;
    if(VarCRef xtm = cfg.lookup("ask_homeserver_timeout"))
        if(const char *stm = xtm.asCString())
        {
            u64 tmp;
            if(strToDurationMS_Safe(&tmp, stm))
                hsTimeout = int(tmp);
        }

    if(!this->homeserver.isValid())
    {
        checkHS = false;
        askHS = false;
    }

    printf("MxSearchHandler: max. client request size = %u\n", (unsigned)searchcfg.maxsize);
    printf("MxSearchHandler: avatar_url = %s\n", searchcfg.avatar_url.c_str());
    printf("MxSearchHandler: displayname = %s\n", searchcfg.displaynameField.c_str());
    printf("MxSearchHandler: searching %u fields:\n", (unsigned)searchcfg.fields.size());
    for(MxSearchConfig::Fields::iterator it = searchcfg.fields.begin(); it != searchcfg.fields.end(); ++it)
        printf(" + %s [fuzzy = %u]\n", it->first.c_str(), it->second.fuzzy);
    printf("MxSearchHandler: Reverse proxy enabled: %s\n", reverseproxy ? "yes" : "no");
    printf("MxSearchHandler: Ask homeserver: %s\n", askHS ? "yes" : "no");
    printf("MxSearchHandler: Ask homeserver timeout: %d ms\n", hsTimeout);
    printf("MxSearchHandler: Check homeserver: %s\n", checkHS ? "yes" : "no");

    // FIXME: remove this again in dtor
    sources.addListener(&this->search);
}

void MxSearchHandler::doSearch(VarRef dst, const char* term, size_t limit) const
{
    const TwoWayCasefoldMatcher matcher(term, strlen(term));

    MxSearch::Matches hits = search.searchExact(matcher);

    // keep best matches, drop the rest if above the limit
    bool limited = false;
    std::sort(hits.begin(), hits.end());
    if(hits.size() > limit)
    {
        hits.resize(limit);
        limited = true;
    }

    // resolve matches to something readable
    const MxStore::SearchResults results = _store.formatMatches(searchcfg, hits.data(), hits.size());

    dst.makeMap().v->map()->clear(*dst.mem); // make sure it's an empty map

    dst["limited"] = limited;
    VarRef ra = dst["results"].makeArray(results.size());

    const bool useAvatar = !searchcfg.avatar_url.empty();
    for (size_t i = 0; i < results.size(); ++i)
    {
        VarRef d = ra.at(i).makeMap();
        const MxStore::SearchResult& r = results[i];
        if (useAvatar)
            d["avatar_url"] = searchcfg.avatar_url.c_str();
        if (!r.displayname.empty())
            d["display_name"] = r.displayname.c_str();
        d["user_id"] = r.str.c_str();
    }
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
                    std::string term; // not const char * on purpose!

                    if(VarCRef xlimit = vars.root().lookup("limit"))
                        if(const u64 *plimit = xlimit.asUint())
                            limit = size_t(*plimit);

                    if(VarCRef xterm = vars.root().lookup("search_term"))
                        if(const char *pterm = xterm.asCString())
                            term = pterm;

                    if(!term.length())
                    {
                        mg_send_http_error(conn, 400, "search_term not provided");
                        return 400;
                    }

                    DataTree out(DataTree::TINY);

                    if(askHS)
                    {
                        // kick off search in background + re-use vars
                        // (this invalidates all strings, that's why term is std::string)
                        vars.root().v->map()->clear(vars);
                        auto fut = std::async(&MxSearchHandler::doSearch, this, vars.root(), term.c_str(), limit);

                        // forward client request as-is
                        MxGetJsonResult jr = mxRequestJson(RQ_POST, out.root(), this->homeserver, vars.root(), hsTimeout);
                        bool useHS = jr.code == MXGJ_OK && !out.root().lookup("error");
                        if(checkHS)
                        {
                            if(jr.code != MXGJ_OK)
                            {
                                mg_send_http_error(conn, 500, "Homeserver did not send usable JSON. Interally reported error:\n%d %s", jr.httpstatus, jr.errmsg.c_str());
                                return 500;
                            }
                            if(!useHS)
                            {
                                std::string jsonerr = dumpjson(out.root());
                                mg_send_http_error(conn, jr.httpstatus, "%s", jsonerr.c_str());
                                return jr.httpstatus;
                            }
                        }

                        if(!useHS)
                            out.root().makeMap().v->map()->clear(out);

                        fut.wait();
                        out.root().merge(vars.root(), MERGE_APPEND_ARRAYS | MERGE_RECURSIVE);
                    }
                    else
                        doSearch(out.root(), term.c_str(), limit);

                    writeJson(dst, out.root(), false);
                    return 0;
                }
            }
        }
    }

    if(reverseproxy)
        return MxReverseProxyHandler::onRequest(dst, conn, rq);

    return HANDLER_FALLTHROUGH;
}

MxReverseProxyHandler::MxReverseProxyHandler(VarCRef cfg)
    : RequestHandler(ClientPrefix, MimeType)
{
    homeserver.load(cfg.lookup("homeserver"));
}

int MxReverseProxyHandler::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    printf("ReverseProxy: %s\n", rq.query.c_str());
    mg_send_http_error(conn, 404, "");
    return 404;
}
