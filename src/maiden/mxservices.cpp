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
    , checkHS(true), askHS(true), overrideAvatar(false), overrideDisplayname(false)
{
    homeserver.timeout = 0;
}

MxSearchHandler::~MxSearchHandler()
{
    _sources.removeListener(&this->search);
}

static bool initOneServer(MxSearchHandler::ServerConfig& srv, VarCRef x)
{
    if(!srv.target.load(x))
    {
        logerror("MxSearchHandler: Failed to apply server setting");
        return false;
    }

    srv.timeout = -1;
    if(VarCRef xtm = x.lookup("timeout"))
    {
        bool ok = false;
        if(const char *stm = xtm.asCString())
        {
            u64 tmp;
            ok = strToDurationMS_Safe(&tmp, stm);
            if(ok)
                srv.timeout = int(tmp);
        }
        if(!ok)
        {
            logerror("MxSearchHandler: server.timeout: Failed to decode time");
            return false;
        }
    }

    srv.authToken.clear();
    if(VarCRef xtok = x.lookup("token"))
    {
        if(const char *tok = xtok.asCString())
            srv.authToken = tok;
        else
        {
            logerror("MxSearchHandler: server.token: Expected string");
            return false;
        }
    }

    return true;
}

inline static const char *yesno(bool x) { return x ? "yes" : "no"; }
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

    /*if (VarCRef xfuzzy = cfg.lookup("fuzzy"))
        searchcfg.fuzzy = xfuzzy && xfuzzy.asBool();*/

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

    if (VarCRef xdd = cfg.lookup("debug_dummy_result"))
        searchcfg.debug_dummy_result = xdd && xdd.asBool();

    if (VarCRef xdd = cfg.lookup("overrideDisplayname"))
        overrideDisplayname = xdd && xdd.asBool();

    if (VarCRef xdd = cfg.lookup("overrideAvatar"))
        overrideAvatar = xdd && xdd.asBool();

    askHS = false;
    checkHS = false;
    if(VarCRef xhs = cfg.lookup("homeserver"))
    {
        askHS = true;
        checkHS = true; // true by default, can be disabled if needed
        if(!initOneServer(homeserver, xhs))
        {
            logerror("MxSearchHandler: Failed to apply homeserver setting");
            return false;
        }

        if (VarCRef x = xhs.lookup("check"))
            checkHS = x && x.asBool();
    }

    if(VarCRef xos = cfg.lookup("other_servers"))
    {
        if(xos.type() == Var::TYPE_ARRAY)
        {
            size_t n = xos.size();
            otherServers.resize(n);
            const Var *a = xos.v->array();
            for(size_t i = 0; i < n; ++i)
                if(!initOneServer(otherServers[i], VarCRef(xos.mem, a)))
                {
                    logerror("MxSearchHanfler: Failed to apply other_servers[%u] setting", (unsigned)i);
                    return false;
                }
        }
        else
            logerror("MxSearchHandler: other_servers is present but not array, can't load");
    }

    if(VarCRef xa = cfg.lookup("accessKeys"))
    {
        if(const Var::Map *m = xa.v->map())
        {
            for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
            {
                PoolStr ks = xa.mem->getSL(it.key());
                const Var& val = it.value();
                if(ks.len)
                {
                    AccessKeyConfig &kc = accessKeys[std::string(ks.s, ks.s + ks.len)];
                    kc.enabled = val.asBool();
                    DEBUG_LOG("MxSearchHandler: Add accessKey [%s], enabled = %u", ks.s, kc.enabled);
                }
            }
        }
        else
            logerror("MxSearchHandler: accessKeys is present but not map, ignoring");
    }


    logdebug("MxSearchHandler: max. client request size = %u", (unsigned)searchcfg.maxsize);
    logdebug("MxSearchHandler: avatar_url = %s", searchcfg.avatar_url.c_str());
    logdebug("MxSearchHandler: displayname = %s", searchcfg.displaynameField.c_str());
    //logdebug("MxSearchHandler: fuzzy global search = %d", searchcfg.fuzzy);
    logdebug("MxSearchHandler: Element substring HACK = %d", searchcfg.element_hack);
    logdebug("MxSearchHandler: searching %u fields:", (unsigned)searchcfg.fields.size());
    for(MxSearchConfig::Fields::iterator it = searchcfg.fields.begin(); it != searchcfg.fields.end(); ++it)
        logdebug(" + %s", it->first.c_str());
    logdebug("MxSearchHandler: Ask homeserver: %s", yesno(askHS));
    logdebug("MxSearchHandler: Ask homeserver timeout: %d ms", homeserver.timeout);
    logdebug("MxSearchHandler: Check homeserver: %s", yesno(checkHS));
    logdebug("MxSearchHandler: debug_dummy_result: %s", yesno(searchcfg.debug_dummy_result));
    logdebug("MxSearchHandler: Override displayname: %s, avatar: %s", yesno(overrideDisplayname), yesno(overrideAvatar));
    logdebug("MxSearchHandler: Forward requests to %u other servers", (unsigned)otherServers.size());

    _sources.addListener(&this->search);
    return true;
}

// converts json-style dynamically typed data to a proper struct
static MxSearchResults convertHsResults(VarCRef hsResultsArray)
{
    MxSearchResults ret;

    if(hsResultsArray && hsResultsArray.type() == Var::TYPE_ARRAY)
    {
        size_t hsNum = hsResultsArray.size();
        ret.reserve(hsNum);
        const Var *hsa = hsResultsArray.v->array();
        for(size_t i = 0; i < hsNum; ++i)
        {
            const VarCRef x(hsResultsArray.mem, &hsa[i]);
            if(VarCRef mxidRef = x.lookup("user_id", 7))
            {
                PoolStr mxid = mxidRef.asString();
                if(mxid.s)
                {
                    MxSearchResult e;
                    e.mxid.assign(mxid.s, mxid.len);

                    if(VarCRef avatarRef = x.lookup("avatar_url", 10))
                        if(const char *avatar = avatarRef.asCString())
                            e.avatar = avatar;
                    if(VarCRef dispRef = x.lookup("display_name", 12))
                        if(const char *disp = dispRef.asCString())
                            e.displayname = disp;

                    ret.push_back(std::move(e));
                }
            }
        }
    }

    return ret;
}

MxSearchResults MxSearchHandler::mergeResults(const MxSearchResults& myresults, const MxSearchResults& hsresults) const
{
    // avoid O(n^2)
    typedef std::unordered_map<std::string_view, size_t> Str2Idx;
    Str2Idx mxid2idx;
    for(size_t i = 0; i < hsresults.size(); ++i)
        mxid2idx[hsresults[i].mxid] = i;

    // First, append the HS results (the authoritive source)
    MxSearchResults res = hsresults;

    const bool overrideAvatar = this->overrideAvatar;
    const bool overrideDisplayname = this->overrideDisplayname;

    // Fix up each HS result if already present or append a new one
    // Both lists are kind-of ordered by match quality, so keep that order if possible
    for(size_t i = 0; i < myresults.size(); ++i)
    {
        const MxSearchResult& my = myresults[i];

        Str2Idx::iterator it = mxid2idx.find(my.mxid);
        if(it != mxid2idx.end())
        {
            MxSearchResult& hs = res[it->second]; // indexes are the same
            assert(hs.mxid == my.mxid);

            // Fill in missing data, or override if configured to do so
            if(overrideDisplayname || hs.displayname.empty())
            {
                DEBUG_LOG("Override displayname '%s' with '%s'", hs.displayname.c_str(), my.displayname.c_str());
                hs.displayname = my.displayname;
            }
            if(overrideAvatar || hs.avatar.empty())
                hs.avatar = my.avatar;
        }

        // Was not there, append
        res.push_back(my);
    }

    return res;
}

// Workaround for https://github.com/matrix-org/matrix-react-sdk/pull/9556
#include "utf8casefold.h"
void MxSearchHandler::_ApplyElementHack(MxSearchResults& results, const std::string& term)
{
    TwoWayCasefoldMatcher fullmatch(term.c_str(), term.length());
    const std::string suffix = "  // " + term;
    std::vector<unsigned char> tmp;

    const size_t N = results.size();
    for(size_t i = 0; i < N; ++i)
    {
        MxSearchResult& sr = results[i];
        bool found = fullmatch.match(sr.mxid.c_str(), sr.mxid.length());

        if (!found)
        {
            tmp.clear();
            const size_t dnlen = sr.displayname.length();
            tmp.reserve(dnlen + 1);
            utf8casefoldcopy(tmp, sr.displayname.c_str(), dnlen);
            tmp.push_back(0);
            found = fullmatch.match((const char*)tmp.data(), tmp.size() - 1); // don't include \0
        }

        if(!found)
            sr.displayname += suffix; // trick element's substring check for the search term into always succeeding
    }
}

const MxSearchHandler::AccessKeyConfig* MxSearchHandler::checkAccessKey(const std::string& token) const
{
    AccessKeyMap::const_iterator it = accessKeys.find(token);
    return it != accessKeys.end() ? &it->second : NULL;
}

MxSearchHandler::MxSearchResultsEx MxSearchHandler::doSearch(const char* term, size_t limit, VarCRef hsResultsArray) const
{
    const std::vector<TwoWayCasefoldMatcher> matchers = mxBuildMatchersForTerm(term);
    {
        std::ostringstream os;
        os << "MxSearchHandler [" << term << "] -> " << matchers.size() << " matchers: ";
        for(size_t i = 0; i < matchers.size(); ++i)
            os << '[' << matchers[i].needle() << ']';
        logdebug("%s", os.str().c_str());
    }

    MxSearch::Matches hits = search.search(matchers);
    const size_t totalhits = hits.size();

    // Go throush results provided by HS, if any
    MxSearchResults hsresults = convertHsResults(hsResultsArray);

    // keep best matches, drop the rest if above the limit
    bool limited = false;
    std::sort(hits.begin(), hits.end());
    if(hits.size() > limit)
        limited = true;

    // resolve matches to something readable
    MxSearchResults myresults = _sources.formatMatches(searchcfg, hits.data(), hits.size(), hsresults, limit);

    // Now we have up to limit many entries on both sides (HS and ours). Merge both.
    MxSearchResultsEx rx = { mergeResults(myresults, hsresults), false };

    // Clip again if too many
    if(rx.results.size() > limit)
    {
        rx.results.resize(limit);
        limited = true;
    }

    if(searchcfg.debug_dummy_result)
    {
        if(hits.size() && hits.size() == limit)
            hits.pop_back(); // make room for the dummy entry

        std::ostringstream os;
        os << "SEARCH[" << term << "] DEBUG: " << totalhits << " hits, limit " << limit
           << ", " << matchers.size() << " matchers: ";
        for (size_t i = 0; i < matchers.size(); ++i)
            os << '[' << matchers[i].needle() << ']';

        MxSearchResult dummy;
        dummy.displayname = os.str();
        dummy.mxid = "@debug_dummy_result:localhost"; // matrix spec requires this to exist
        rx.results.insert(rx.results.begin(), std::move(dummy));
    }

    rx.limited = limited;
    return rx;
}

void MxSearchHandler::translateResults(VarRef dst, const MxSearchResultsEx& rx) const
{
    dst.makeMap().v->map()->clear(*dst.mem); // make sure it's an empty map

    dst["limited"] = rx.limited;
    const bool useGlobalAvatar = !searchcfg.avatar_url.empty();


    VarRef ra = dst["results"].makeArray(rx.results.size());

    for (size_t i = 0; i < rx.results.size(); ++i)
    {
        VarRef d = ra.at(i).makeMap();
        const MxSearchResult& r = rx.results[i];

        VarRef mxid = d["user_id"];
        StrRef mxidRef = mxid.setStr(r.mxid.c_str(), r.mxid.length());

        if (!r.avatar.empty())
            d["avatar_url"] = r.avatar.c_str();
        else if (useGlobalAvatar)
            d["avatar_url"] = searchcfg.avatar_url.c_str();

        if (!r.displayname.empty())
            d["display_name"] = r.displayname.c_str();
        d["user_id"] = r.mxid.c_str();
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
                VarCRef xhsResultsArray;
                bool raw = false;
                bool forwardRequest = askHS;

                if(const AccessKeyConfig *acfg = checkAccessKey(rq.authorization))
                {
                    // We're in relay mode -- don't go to the HS.
                    // Someone with special authorization is asking, so we just provide
                    // from our own cache, without massaging the results in any way.
                    if(acfg->enabled)
                    {
                        raw = true;
                        forwardRequest = false;
                    }
                    // else we send the known-bad key to the HS, that's going to refuse it as usual,
                    // and an eventual attacker will be none the wiser.
                    // ie. we don't send a specialized error message about a disabled key on purpose.
                }

                if(forwardRequest)
                {
                    bool askThisTime = true;

                    // forward client request
                    Var hvar;
                    VarRef headers(vars, &hvar);
                    // without auth, this is going to fail because the HS will say no
                    if(!rq.authorization.empty())
                    {
                        AccessKeyMap::const_iterator it = accessKeys.find(rq.authorization);
                        if(it != accessKeys.end() && it->second.enabled)
                        {
                            askThisTime = false;
                            DEBUG_LOG("MxSearchHandler: Found authorization in accessKeys, skipping HS check");
                        }

                        if(askThisTime)
                            headers["Authorization"] = rq.authorization.c_str();
                    }

                    if(askThisTime)
                    {
                        URLTarget hs = this->homeserver.target;
                        hs.path = ClientPrefix + rq.query; // forward URL as-is
                        ScopeTimer tm;
                        MxGetJsonResult jr = mxSendRequest(RQ_POST, hsdata.root(), hs, RQFMT_JSON | RQFMT_BJ, vars.root(), headers, this->homeserver.timeout);
                        logdev("mxRequestJson done after %u ms, result = %u", (unsigned)tm.ms(), jr.code);
                        headers.clear();
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

                        VarRef xresults = hsdata.root().lookup("results");
                        if(xresults && xresults.type() == Var::TYPE_ARRAY)
                        {
                            xhsResultsArray = xresults;
                            const size_t n = xresults.size();
                            logdev("HS found %zu users", n);
                        }
                        else
                            logerror("HS didn't send results array! (This is against the spec)");
                    }
                }

                assert(!term.empty());

                MxSearchResultsEx rx = doSearch(term.c_str(), limit, xhsResultsArray);

                if(!raw && searchcfg.element_hack)
                    _ApplyElementHack(rx.results, term);

                // re-use vars for the search since we don't need those anymore
                translateResults(vars.root(), rx);

                // send in json, unless the requesting client supports BJ (relay mode)
                serialize::Format fmt = serialize::JSON;
                if(rq.fmt & RQFMT_BJ)
                    fmt = serialize::BJ;

                serialize::save(dst, vars.root(), fmt);

                return 0;
            }
        }
    }

    mg_send_http_error(conn, 404, "MxSearchHandler: not found");
    return 404;
}
