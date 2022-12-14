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
#include <sstream>

static const char *MimeType = "application/json";
static const char  WellknownPrefix[] = "/.well-known/matrix";

// TODO: see https://github.com/civetweb/civetweb/blob/master/include/civetweb.h#L489
static const char  ClientPrefix[] = "/_matrix/client";
static const char  ClientSearchPostfix[] = "/user_directory/search";

// The search endpoint changed in v0.4.0 of the matrix spec; we support any version tag in between
//static const char SearchPrefix_v04[] = "/_matrix/client/r0/user_directory/search";
//static const char SearchPrefix_v3[] = "/_matrix/client/v3/user_directory/search";

MxWellknownHandler::MxWellknownHandler()
    : RequestHandler(WellknownPrefix, MimeType)
{

}

bool MxWellknownHandler::init(VarCRef cfg)
{
    const Var::Map *m = cfg ? cfg.v->map() : NULL;
    if(m)
        for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
        {
            const char *key = cfg.mem->getS(it.key());
            VarCRef value(cfg.mem, &it.value());
            data[key] = dumpjson(value);
        }
    return !!m;
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
    logdebug("wellknown:%s: %s", info->request_method, info->local_uri_raw);
    mg_send_http_ok(conn, "application/json", resp->length());
    mg_response_header_add(conn, "Access-Control-Allow-Origin", "*", 1);
    mg_write(conn, resp->c_str(), resp->length());
    return 200;
}

MxSearchHandler::MxSearchHandler(MxSources& sources)
    : RequestHandler(ClientPrefix, MimeType), search(searchcfg), _sources(sources)
    , checkHS(true), askHS(true), hsTimeout(0)
{
}

MxSearchHandler::~MxSearchHandler()
{
    _sources.removeListener(&this->search);
}

bool MxSearchHandler::init(VarCRef cfg)
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
                        /*VarCRef f(xfields.mem, &val);
                        VarCRef xfuzzy = f.lookup("fuzzy");
                        fcfg.fuzzy = xfuzzy && xfuzzy.asBool();*/
                        assert(false); // atm not supported
                    }
                    else
                    {
                        logerror("search->fields->%s has unhandled type, ignoring", ps.s);
                    }
                }
                else
                {
                    logerror("search->fields has non-string element");
                }
            }
        }
        else
        {
            logerror("search->fields is present but not map, ignoring");
        }
    }

    if (VarCRef xfuzzy = cfg.lookup("fuzzy"))
        searchcfg.fuzzy = xfuzzy && xfuzzy.asBool();

    if (VarCRef xeh = cfg.lookup("element_hack"))
        searchcfg.element_hack = xeh && xeh.asBool();

    if(VarCRef xurl = cfg.lookup("avatar_url"))
        if(const char *url = xurl.asCString())
            searchcfg.avatar_url = url;

    if (VarCRef xdn = cfg.lookup("displayname"))
        if (const char* dn = xdn.asCString())
            searchcfg.displaynameField = dn;

    if(VarCRef xmaxsize = cfg.lookup("maxsize"))
        if(const u64 *pmaxsize = xmaxsize.asUint())
            searchcfg.maxsize = size_t(*pmaxsize);

    if (VarCRef xproxy = cfg.lookup("check_homeserver"))
        checkHS = xproxy && xproxy.asBool();

    if (VarCRef xproxy = cfg.lookup("ask_homeserver"))
        askHS = xproxy && xproxy.asBool();

    if (VarCRef xdd = cfg.lookup("debug_dummy_result"))
        searchcfg.debug_dummy_result = xdd && xdd.asBool();

    hsTimeout = -1;
    if(VarCRef xtm = cfg.lookup("ask_homeserver_timeout"))
        if(const char *stm = xtm.asCString())
        {
            u64 tmp;
            if(strToDurationMS_Safe(&tmp, stm))
                hsTimeout = int(tmp);
        }

    if(VarCRef xhs = cfg.lookup("homeserver"))
    {
        if(!homeserver.load(xhs))
        {
            logerror("MxSearchHandler: Failed to apply homeserver setting");
            return false;
        }
    }

    if(!this->homeserver.isValid())
    {
        checkHS = false;
        askHS = false;
    }

    logdebug("MxSearchHandler: max. client request size = %u", (unsigned)searchcfg.maxsize);
    logdebug("MxSearchHandler: avatar_url = %s", searchcfg.avatar_url.c_str());
    logdebug("MxSearchHandler: displayname = %s", searchcfg.displaynameField.c_str());
    logdebug("MxSearchHandler: fuzzy global search = %d", searchcfg.fuzzy);
    logdebug("MxSearchHandler: Element substring HACK = %d", searchcfg.element_hack);
    logdebug("MxSearchHandler: searching %u fields:", (unsigned)searchcfg.fields.size());
    for(MxSearchConfig::Fields::iterator it = searchcfg.fields.begin(); it != searchcfg.fields.end(); ++it)
        logdebug(" + %s", it->first.c_str());
    logdebug("MxSearchHandler: Ask homeserver: %s", askHS ? "yes" : "no");
    logdebug("MxSearchHandler: Ask homeserver timeout: %d ms", hsTimeout);
    logdebug("MxSearchHandler: Check homeserver: %s", checkHS ? "yes" : "no");
    logdebug("MxSearchHandler: debug_dummy_result: %s", searchcfg.debug_dummy_result ? "yes" : "no");

    _sources.addListener(&this->search);
    return true;
}

void MxSearchHandler::doSearch(VarRef dst, const char* term, size_t limit) const
{
    const std::vector<TwoWayCasefoldMatcher> matchers = mxBuildMatchersForTerm(term);
    {
        std::ostringstream os;
        os << "MxSearchHandler [" << term << "] -> " << matchers.size() << " matchers: ";
        for(size_t i = 0; i < matchers.size(); ++i)
            os << '[' << matchers[i].needle() << ']';
        logdebug("%s", os.str().c_str());
    }

    TwoWayCasefoldMatcher fullmatch(term, strlen(term));
    MxSearch::Matches hits = search.search(matchers, searchcfg.fuzzy, searchcfg.element_hack ? &fullmatch : NULL);
    const size_t totalhits = hits.size();

    // keep best matches, drop the rest if above the limit
    bool limited = false;
    std::sort(hits.begin(), hits.end());
    if(hits.size() > limit)
    {
        hits.resize(limit);
        limited = true;
    }

    if(hits.size() && hits.size() == limit && searchcfg.debug_dummy_result)
        hits.pop_back(); // make room for the dummy entry

    // resolve matches to something readable
    MxSources::SearchResults results = _sources.formatMatches(searchcfg, hits.data(), hits.size(), term);

    dst.makeMap().v->map()->clear(*dst.mem); // make sure it's an empty map

    dst["limited"] = limited;
    VarRef ra = dst["results"].makeArray(results.size());

    const bool useAvatar = !searchcfg.avatar_url.empty();
    const bool elementHack = searchcfg.element_hack;

    if(searchcfg.debug_dummy_result)
    {
        std::ostringstream os;
        os << "SEARCH[" << term << "] DEBUG: " << totalhits << " hits, limit " << limit
           << ", " << matchers.size() << " matchers: ";
        for (size_t i = 0; i < matchers.size(); ++i)
            os << '[' << matchers[i].needle() << ']';

        MxSources::SearchResult dummy;
        dummy.displayname = os.str();
        dummy.str = "@debug_dummy_result:localhost"; // matrix spec requires this to exist
        results.insert(results.begin(), std::move(dummy));
    }

    for (size_t i = 0; i < results.size(); ++i)
    {
        VarRef d = ra.at(i).makeMap();
        MxSources::SearchResult& r = results[i];
        if (useAvatar)
            d["avatar_url"] = searchcfg.avatar_url.c_str();
        if (!r.displayname.empty())
            d["display_name"] = r.displayname.c_str();
        d["user_id"] = r.str.c_str();
    }
}

int MxSearchHandler::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    if(rq.type != RQ_POST)
    {
        mg_send_http_error(conn, 405, "expected POST");
        return 405;
    }

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

                DataTree hsdata(DataTree::TINY);
                bool useHS = false;
                if(askHS)
                {
                    // forward client request as-is
                    MxGetJsonResult jr = mxRequestJson(RQ_POST, hsdata.root(), this->homeserver, vars.root(), hsTimeout);
                    useHS = jr.code == MXGJ_OK && !hsdata.root().lookup("error");
                    if(checkHS)
                    {
                        if(jr.code != MXGJ_OK)
                        {
                            std::string err = jr.getErrorMsg();
                            logerror("-> %s", err.c_str());
                            mg_send_http_error(conn, 500, "Homeserver did not send usable JSON. Internally reported error:\n%s", err.c_str());
                            return 500;
                        }
                        if(!useHS)
                        {
                            std::string jsonerr = dumpjson(hsdata.root());
                            mg_send_http_error(conn, jr.httpstatus, "%s", jsonerr.c_str());
                            return jr.httpstatus;
                        }
                    }
                }
                
                // out is now whatever the HS returned (if any), re-use vars for the search since we don't need those anymore
                vars.root().v->makeMap(vars, 0)->clear(vars);
                doSearch(vars.root(), term.c_str(), limit);

                // merge HS results on top (HS results win and override our own search results)
                if(useHS)
                    vars.root().merge(hsdata.root(), MERGE_APPEND_ARRAYS | MERGE_RECURSIVE);

                // TODO: respect limit

                writeJson(dst, vars.root(), false);
                return 0;
            }
        }
    }

    mg_send_http_error(conn, 404, "MxSearchHandler: not found");
    return 404;
}
