#include "handler_mxid_v2.h"
#include <sstream>
#include <string>
#include "debugfunc.h"
#include "json_out.h"
#include "json_in.h"
#include "mxstore.h"
#include "request.h"
#include "mxdefines.h"
#include "mxresolv.h"
#include <civetweb/civetweb.h>
#include "mxhttprequest.h"
#include "mxtoken.h"
#include "tomcrypt/tomcrypt.h"


// All /account/register tokens we issue start with this
#define TOKEN_PREFIX "mai_"
#define TOKEN_PREFIX_LEN 4

#define MIMETYPE "application/json; charset=utf-8"

// anything after /_matrix/identity/v2
const MxidHandler_v2::Endpoint MxidHandler_v2::s_endpoints[] =
{
    { RQ_GET,  AUTHED, "/account",                       &MxidHandler_v2::get_account  },
    { RQ_POST, AUTHED, "/account/logout",                &MxidHandler_v2::post_account_logout  },
    { RQ_POST, NOAUTH, "/account/register",              &MxidHandler_v2::post_account_register  },
    { RQ_GET,  AUTHED, "/terms",                         &MxidHandler_v2::get_terms  },
    { RQ_POST, NOAUTH, "/terms",                         &MxidHandler_v2::post_terms  },
    { RQ_GET,  NOAUTH, "/",                              &MxidHandler_v2::get_status  }, // status check
    { RQ_GET,  NOAUTH, "/pubkey/ephemeral/isvalid",      &MxidHandler_v2::get_pubkey_eph_isvalid  },
    { RQ_GET,  NOAUTH, "/pubkey/isvalid",                &MxidHandler_v2::get_pubkey_isvalid  },
    { RQ_GET,  NOAUTH, "/pubkey",                        &MxidHandler_v2::get_pubkey  }, // actually pubkey/{keyId}
    { RQ_GET,  AUTHED, "/hash_details",                  &MxidHandler_v2::get_hashdetails  },
    { RQ_POST, AUTHED, "/lookup",                        &MxidHandler_v2::get_lookup  },
    { RQ_POST, AUTHED, "/validate/email/requestToken",   &MxidHandler_v2::get_validate_email_requestToken  },
    { RQ_GET,  AUTHED, "/validate/email/submitToken",    &MxidHandler_v2::get_validate_email_submitToken  },
    { RQ_POST, AUTHED, "/validate/email/submitToken",    &MxidHandler_v2::post_validate_email_submitToken  },
    { RQ_POST, AUTHED, "/validate/msisdn/requestToken",  &MxidHandler_v2::get_validate_msisdn_requestToken  },
    { RQ_GET,  AUTHED, "/validate/msisdn/submitToken",   &MxidHandler_v2::get_validate_msisdn_submitToken  },
    { RQ_POST, AUTHED, "/validate/msisdn/submitToken",   &MxidHandler_v2::post_validate_msisdn_submitToken  },
    { RQ_POST, AUTHED, "/3pid/bind",                     &MxidHandler_v2::post_3pid_bind  },
    { RQ_GET,  AUTHED, "/3pid/getValidated3pid",         &MxidHandler_v2::get_2pid_getValidated3pid  },
    { RQ_POST, AUTHED, "/3pid/unbind",                   &MxidHandler_v2::post_3pid_unbind  },
    { RQ_POST, AUTHED, "/store-invite",                  &MxidHandler_v2::post_store_invite  },
    { RQ_POST, AUTHED, "/sign-ed25519",                  &MxidHandler_v2::post_sign_ed25519  },

    // TODO: extension for https://github.com/ma1uta/matrix-synapse-rest-password-provider
    // (should probably make this a different endpoint/service)

    { RQ_UNKNOWN, NOAUTH, NULL, NULL }
};

// these names can not be passed in via URL
static const char *mountpointForRequestType(RequestType type)
{
    if(type == RQ_GET)
        return "/GET";
    else if(type == RQ_POST)
        return "/POST";
    return NULL;
}

static int _ok(mg_connection *conn)
{
    mg_send_http_ok(conn, MIMETYPE, 2);
    mg_write(conn, "{}", 2);
    return 200;
}

static const char* str(VarCRef ref)
{
    return ref ? ref.asCString() : NULL;
}
static s64 intor0(VarCRef ref)
{
    if (!ref)
        return 0;
    const s64* p = ref.asInt();
    return p ? *p : 0;
}

MxidHandler_v2::MxidHandler_v2(MxStore& mxs, const char* prefix)
    : RequestHandler(prefix, MIMETYPE)
    , _store(mxs)
    , _handlers(DataTree::TINY)
    , _port(8448)
{
    _setupHandlers();
}

MxidHandler_v2::~MxidHandler_v2()
{
}

static int sendError(mg_connection* conn, int status, MxError err, const char* extra = 0)
{
    const char* estr = mxErrorStr(err);
    if (!extra)
        extra = estr;
    mg_send_http_error(conn, status, "{\"errcode\":\"%s\",\"error\":\"%s\"}", estr, extra);
    return status;
}

static int nyi(mg_connection* conn)
{
    mg_send_http_error(conn, 404, "not yet implemented");
    return 404;
}


static int sendErrorEx(mg_connection* conn, VarRef dst, int status, MxError err, const char* extra = 0)
{
    const char* estr = mxErrorStr(err);
    dst["errcode"] = estr;
    dst["error"] = extra ? extra : estr;
    std::string tmp = dumpjson(dst, false);
    mg_send_http_error(conn, status, "%s", tmp.c_str());
    return status;
}

int MxidHandler_v2::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    VarCRef ref = _handlers.subtreeConst(rq.query.c_str());
    if(ref.type() != Var::TYPE_MAP)
        return sendError(conn, 404, M_NOT_FOUND, "Endpoint not found. See spec.matrix.org/v1.2/identity-service-api/");

    const char *mp = mountpointForRequestType(rq.type);
    if(!mp)
        return sendError(conn, 405, M_UNKNOWN, "Unknown HTTP request type");

    VarCRef vp = ref.lookup(mp);
    if(!vp || vp.type() != Var::TYPE_PTR)
        return sendError(conn, 405, M_UNKNOWN, "Method Not Allowed (did you just try to GET a POST-only resource?)");

    // TODO: send CORS headers

    const Endpoint * const ep = static_cast<const Endpoint*>(vp.asPtr());

    // Handle user authorization if the endpoint requires it
    UserInfo user;
    const AuthResult au = _tryAuthorize(conn, rq);
    if(ep->auth != NOAUTH)
    {
        if(au.err != M_OK)
            return sendError(conn, 403, au.err, "Invalid/unknown token");

        user.token = au.token;
        user.username = _store.getAccount(au.token);
        if(user.username.empty())
            return sendError(conn, 500, M_UNRECOGNIZED, "Authorization succeeded but no user is associated with the token");
        user.auth = AUTHED;
    }

    // If the user supplied an invalid token (but DID supply a token) this will still succeed if the endpoint does not require authorization
    if(user.auth < ep->auth)
        return sendError(conn, 403, M_UNKNOWN, "Action requires authorization (must send 'Authorization: Bearer " TOKEN_PREFIX "<token>' HTTP header)");

    return (this->*ep->func)(dst, conn, rq, user);
}

void MxidHandler_v2::_setupHandlers()
{
    for(size_t i = 0; s_endpoints[i].endpoint; ++i)
    {
        const Endpoint& ep = s_endpoints[i];
        VarRef ref = _handlers.subtree(ep.endpoint, Var::SQ_CREATE);
        const char *mp = mountpointForRequestType(ep.type);
        assert(mp);
        ref[mp] = (void*)&ep;
    }
}

static bool extractToken(char *dst, size_t avail,  mg_connection* conn, const Request& rq)
{
    const char *token = NULL;
    if(rq.authorization.size() && !mg_strncasecmp(rq.authorization.c_str(), "Bearer", 6))
    {
        const char *s = rq.authorization.c_str() + 6;
        size_t ws = 0;
        while(isspace(*s))
        {
            ++ws;
            ++s;
        }
        if(ws)
            token = s;
    }
    const mg_request_info *info = mg_get_request_info(conn);
    if(!token && info->query_string && *info->query_string)
    {
        // TODO: this is really not efficient
        DataTree params(DataTree::TINY);
        int n = Request::ReadQueryVars(params.root(), info);
        if(n > 0)
        {
            VarCRef ref = params.root()["access_token"];
            if(ref)
                token = ref.asCString();
        }
    }
    if(token && *token && !strncmp(token, TOKEN_PREFIX, TOKEN_PREFIX_LEN))
    {
        strncpy(dst, token + TOKEN_PREFIX_LEN, avail);
        return true;
    }
    return false;
}


MxidHandler_v2::AuthResult MxidHandler_v2::_tryAuthorize(mg_connection* conn, const Request& rq) const
{
    AuthResult res;
    res.err = M_OK;
    res.httpstatus = 0;
    bool hastoken = extractToken(res.token, sizeof(res.token), conn, rq);
    if(!hastoken)
    {
        res.err = M_MISSING_PARAMS;
        res.httpstatus = 401;
    }
    else
    {
        res.err = _store.authorize(res.token);
        if(res.err)
            res.httpstatus = 403;
    }

    return res;
}

int MxidHandler_v2::get_account(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    dst.WriteStr("{\"user_id\": \"");
    dst.Write(u.username.c_str(), u.username.length());
    dst.WriteStr("\"}");
    return 0;
}

int MxidHandler_v2::post_account_logout(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    _store.logout(u.token.c_str());
    return _ok(conn);
}

int MxidHandler_v2::post_account_register(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    DataTree params(DataTree::TINY);
    if(Request::AutoReadVars(params.root(), conn) <= 0)
        return sendError(conn, 401, M_MISSING_PARAMS);

    const char *sn =    str(params.root().lookup("matrix_server_name"));
    const char *tok =   str(params.root().lookup("access_token"));
    s64 exps = intor0(params.root().lookup("expires_in"));
    const char *tokty = str(params.root().lookup("token_type"));
    if(!tokty || strcmp(tokty, "Bearer"))
        printf("MX-SPEC: Expected token_type == 'Bearer', got '%s'\n", tokty);
    if(exps <= 0)
    {
        printf("MX-SPEC: Expected expires_in > 0\n");
        exps = 0;
    }
    if(!sn || !tok || !*sn || !*tok)
        return sendError(conn, 401, M_MISSING_PARAMS, "Must provide at least matrix_server_name & access_token");

    u64 expMS = u64(exps) * 1000; // make unsigned & millis
    u64 maxexpMS = _store.getConfig().register_.maxTime;
    if(!expMS || expMS > maxexpMS)
        expMS = maxexpMS;

    // MX-SPEC: we should be reading the request body and ensure it's at least {}
    // but since the contents are ignored, who cares

    // get matrix server name from host's .well-known
    MxResolvResult resolv;
    std::string account;
    MxStore::LookupResult res = _store.getCachedHomeserverForHost(sn, resolv.host, resolv.port);
    if(res == MxStore::FAILED)
        return sendError(conn, 502, M_NOT_FOUND, "No homeserver exists for this host (cached)");

    std::ostringstream errors;
    bool storeHS = false;
    MxResolvList list;
    if(res == MxStore::VALID) // already have a good homeserver cached?
    {
        list.push_back(resolv); // just use it
    }
    else // don't have homeserver cached, get a list of candidates
    {
        const MxStore::Config& cfg = _store.getConfig();
        list = lookupHomeserverForHost(sn, cfg.wellknown.requestTimeout, cfg.wellknown.requestMaxSize);
        if(list.empty())
            return sendError(conn, 502, M_NOT_FOUND, "Couldn't resolve homeserver");
        storeHS = true; // did a lookup, store it later
    }

    // try hosts in the list until one works; the list is already sorted by priority
    for(size_t i = 0; i < list.size(); ++i)
    {
        resolv = list[i];
        printf("Contacting homeserver %s for host %s [%u/%u] ...\n",
            resolv.host.c_str(), sn, unsigned(i+1), unsigned(list.size()));
        DataTree tmp(DataTree::TINY);
        std::ostringstream uri;
        uri << "/_matrix/federation/v1/openid/userinfo?access_token=" << tok; // FIXME: quote
        MxGetJsonResult jr = mxGetJson(tmp.root(), resolv.host.c_str(), resolv.port, uri.str().c_str(), 5000, 4*1024);
        switch(jr)
        {
            case MXGJ_OK:
                res = MxStore::VALID; // the host is good
                if(VarCRef sub = tmp.root().lookup("sub"))
                    if(const char *user = sub.asCString())
                        account = user;
                goto out; // get outta the loop
            break;

            case MXGJ_PARSE_ERROR:
                // don't consider this a good host; maybe the webserver config is messed up?
                errors << resolv.host << ": Federation API sent malformed reply\n";
                break; // try next

            case MXGJ_CONNECT_FAILED:
                // unable to connect is what we'd expect from a downed server
                errors << resolv.host << ": Server looks down from here\n";
                break; // try next
        }
    }

out:

    // store valid homeserver only if it managed to deliver a good reply
    if(res == MxStore::VALID)
    {
        if(storeHS)
            _store.storeHomeserverForHost(sn, resolv.host.c_str(), resolv.port);
    }
    else
        return sendError(conn, 502, M_NOT_FOUND, ("Failed to resolve good homeserver, errors:\n" + errors.str()).c_str());

    if(account.empty())
        return sendError(conn, 500, M_NOT_FOUND, (resolv.host + ": Access token does not belong to a known user").c_str());


    // Keep trying until we get un unused token (the chance that this actually loops is pretty much zero)
    char thetoken[44]; // whatev
    do
        mxGenerateToken(thetoken, sizeof(thetoken), false);
    while(!_store.register_(thetoken, sizeof(thetoken), expMS, account.c_str()));

    // Same thing as sydent does: Supply both keys to make older clients happy
    dst.WriteStr("{\"token\": \"" TOKEN_PREFIX);
    dst.Write(thetoken, sizeof(thetoken));
    dst.WriteStr("\", \"access_token\": \"" TOKEN_PREFIX);
    dst.Write(thetoken, sizeof(thetoken));
    // ++ non-standard: to help with debugging ++
    dst.WriteStr("\", \"user_id\": \"");
    dst.WriteStr(account.c_str());
    // ++
    dst.WriteStr("\"}");
    return 0;
}

int MxidHandler_v2::get_terms(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    // TODO: this should be handled in Lua?
    dst.WriteStr("{\"policies\":{}}");
    return 0;
}

int MxidHandler_v2::post_terms(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return 501; // TODO
}

int MxidHandler_v2::get_status(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return _ok(conn);
}

int MxidHandler_v2::get_pubkey_eph_isvalid(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return nyi(conn);
}

int MxidHandler_v2::get_pubkey_isvalid(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return nyi(conn);
}

int MxidHandler_v2::get_pubkey(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return nyi(conn);
}

int MxidHandler_v2::get_hashdetails(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    dst.WriteStr("{\"algorithms\":[\"none\"");
    const MxStore::Config::Hashes& hs = _store.getConfig().hashes;
    size_t n = 0;
    for(MxStore::Config::Hashes::const_iterator it = hs.begin(); it != hs.end(); ++it, ++n)
    {
        if(n)
            dst.Put(',');
        dst.Put('\"');
        dst.WriteStr(it->first.c_str());
        dst.Put('\"');
    }

    dst.WriteStr("], \"lookup_pepper\":\"");
    std::string pepper = _store.getHashPepper(true);
    dst.Write(pepper.c_str(), pepper.length());
    dst.WriteStr("\"}");
    return 0;
}

int MxidHandler_v2::get_lookup(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    DataTree data(DataTree::SMALL);
    if(Request::ReadJsonBodyVars(data.root(), conn) < 0)
        return sendError(conn, 400, M_MISSING_PARAMS, "Failed to parse JSON body");

    VarCRef xaddr = data.root().lookup("addresses");
    VarCRef xalgo = data.root().lookup("algorithm");
    VarCRef xpep = data.root().lookup("pepper");

    if(!(xaddr && xalgo && xpep && xaddr.type() == Var::TYPE_ARRAY))
        return sendError(conn, 400, M_MISSING_PARAMS, "Expected fields not present");

    const char * const algo = xalgo.asCString();
    const char * const pepper = xpep.asCString();
    if(!algo || !pepper)
        return sendError(conn, 400, M_INVALID_PARAM, "Type mismatch");

    // Re-use our own private pool that we already have
    VarRef out = data.root()["_"];
    out.clear(); // for smug clients
    VarRef xdst = out["mappings"].makeMap();

    MxError err = _store.hashedBulkLookup(xdst, xaddr, algo, pepper);
    if(err != M_OK)
    {
        if(err != M_INVALID_PEPPER)
            return sendError(conn, 400, err);
        else // as per MSC2134, the returned error contains also the new pepper
        {
            out.clear();
            out["algorithm"] = algo;
            out["lookup_pepper"] = _store.getHashPepper(false).c_str();
            return sendErrorEx(conn, out, 400, err, "received invalid pepper - was it rotated?");
        }
    }

    writeJson(dst, out, false);
    return 0;
}

int MxidHandler_v2::get_validate_email_requestToken(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return nyi(conn);
}

int MxidHandler_v2::get_validate_email_submitToken(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return nyi(conn);
}

int MxidHandler_v2::post_validate_email_submitToken(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return nyi(conn);
}

int MxidHandler_v2::get_validate_msisdn_requestToken(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return nyi(conn);
}

int MxidHandler_v2::get_validate_msisdn_submitToken(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return nyi(conn);
}

int MxidHandler_v2::post_validate_msisdn_submitToken(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return nyi(conn);
}

int MxidHandler_v2::post_3pid_bind(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return nyi(conn);
}

int MxidHandler_v2::get_2pid_getValidated3pid(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return nyi(conn);
}

int MxidHandler_v2::post_3pid_unbind(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return nyi(conn);
}

int MxidHandler_v2::post_store_invite(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return nyi(conn);
}

int MxidHandler_v2::post_sign_ed25519(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return nyi(conn);
}

