#include "handler_mxid_v2.h"
#include <sstream>
#include <string>
#include "debugfunc.h"
#include "json_out.h"
#include "mxstore.h"
#include "request.h"
#include "mxdefines.h"
#include "mxresolv.h"
#include <civetweb/civetweb.h>
#include "mxhttprequest.h"
#include "mxtoken.h"


// All /account/register tokens we issue start with this
#define TOKEN_PREFIX "mai_"
#define TOKEN_PREFIX_LEN 4

#define MIMETYPE "application/json; charset=utf-8"

// anything after /_matrix/identity/v2
const MxidHandler_v2::Endpoint MxidHandler_v2::s_endpoints[] =
{
    { RQ_GET,  AUTHED, "/account",                       &get_account  },
    { RQ_POST, AUTHED, "/account/logout",                &post_account_logout  },
    { RQ_POST, NOAUTH, "/account/register",              &post_account_register  },
    { RQ_GET,  AUTHED, "/terms",                         &get_terms  },
    { RQ_POST, NOAUTH, "/terms",                         &post_terms  },
    { RQ_GET,  AUTHED, "/",                              &get_status  }, // status check
    { RQ_GET,  NOAUTH, "/pubkey/ephemeral/isvalid",      &get_pubkey_eph_isvalid  },
    { RQ_GET,  NOAUTH, "/pubkey/isvalid",                &get_pubkey_isvalid  },
    { RQ_GET,  NOAUTH, "/pubkey",                        &get_pubkey  }, // actually pubkey/{keyId}
    { RQ_GET,  AUTHED, "/hash_details",                  &get_hashdetails  },
    { RQ_POST, AUTHED, "/lookup",                        &get_lookup  },
    { RQ_POST, AUTHED, "/validate/email/requestToken",   &get_validate_email_requestToken  },
    { RQ_GET,  AUTHED, "/validate/email/submitToken",    &get_validate_email_submitToken  },
    { RQ_POST, AUTHED, "/validate/email/submitToken",    &post_validate_email_submitToken  },
    { RQ_POST, AUTHED, "/validate/msisdn/requestToken",  &get_validate_msisdn_requestToken  },
    { RQ_GET,  AUTHED, "/validate/msisdn/submitToken",   &get_validate_msisdn_submitToken  },
    { RQ_POST, AUTHED, "/validate/msisdn/submitToken",   &post_validate_msisdn_submitToken  },
    { RQ_POST, AUTHED, "/3pid/bind",                     &post_3pid_bind  },
    { RQ_GET,  AUTHED, "/3pid/getValidated3pid",         &get_2pid_getValidated3pid  },
    { RQ_POST, AUTHED, "/3pid/unbind",                   &post_3pid_unbind  },
    { RQ_POST, AUTHED, "/store-invite",                  &post_store_invite  },
    { RQ_POST, AUTHED, "/sign-ed25519",                  &post_sign_ed25519  },
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
    std::ostringstream os;
    const char* estr = mxErrorStr(err);
    if (!extra)
        extra = estr;
    mg_send_http_error(conn, status, "{\"errcode\":\"%s\",\"error\":\"%s\"}", estr, extra);
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
    if(au.err != M_OK)
        return sendError(conn, 403, au.err, "Invalid/unknown token");

    user.token = au.token;
    user.username = _store.getAccount(au.token);
    if(user.username.empty())
        return sendError(conn, 500, M_UNRECOGNIZED, "Authorization succeeded but no user is associated with the token");
    user.auth = AUTHED;

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
    s64 exp = intor0(params.root().lookup("expires_in"));
    const char *tokty = str(params.root().lookup("token_type"));
    if(!tokty || strcmp(tokty, "Bearer"))
        printf("MX-SPEC: Expected token_type == 'Bearer', got '%s'\n", tokty);
    if(!exp)
        printf("MX-SPEC: Expected expires_in > 0\n");
    if(!sn || !tok || !*sn || !*tok)
        return sendError(conn, 401, M_MISSING_PARAMS, "Must provide at least matrix_server_name & access_token");

    if(!exp)
        exp = 3600; // FIXME: make configurable

    // MX-SPEC: we should be reading the request body and ensure it's at least {}
    // but since the contents are ignored, who cares

    // get matrix server name from host's .well-known
    MxResolvResult resolv;
    MxStore::LookupResult res = _store.getCachedHomeserverForHost(sn, resolv.host, resolv.port);
    if(res == MxStore::FAILED)
        return sendError(conn, 502, M_NOT_FOUND, "No homeserver exists for this host (cached)");

    if(res != MxStore::VALID)
    {
        resolv = lookupHomeserverForHost(sn, 5000, 1024*50); // TODO: make configurable
        if(!resolv.host.empty())
        {
            _store.storeHomeserverForHost(sn, resolv.host.c_str(), resolv.port);
        }

    }

    if(resolv.host.empty())
        return sendError(conn, 502, M_NOT_FOUND, "Failed to resolve homeserver");

    std::string account;
    {
        DataTree tmp(DataTree::TINY);
        std::ostringstream uri;
        uri << "/_matrix/federation/v1/openid/userinfo?access_token=" << tok; // FIXME: quote
        int r = mxGetJson(tmp.root(), resolv.host.c_str(), resolv.port, uri.str().c_str(), 5000, 4*1024);
        if(r <= 0)
            return sendError(conn, 500, M_UNKNOWN, (resolv.host + ": Federation API sent malformed reply").c_str());

        if(VarCRef sub = tmp.root().lookup("sub"))
            if(const char *user = sub.asCString())
                account = user;
    }

    if(account.empty())
        return sendError(conn, 500, M_NOT_FOUND, (resolv.host + ": Access token does not belong to a known user").c_str());


    // Keep trying until we get un unused token (the chance that this actually loops is pretty much zero)
    char thetoken[44]; // whatev
    do
        mxGenerateToken(thetoken, sizeof(thetoken));
    while(!_store.register_(thetoken, exp, account.c_str()));

    // Same thing as sydent does: Supply both keys to make older clients happy
    dst.WriteStr("{\"token\": \"" TOKEN_PREFIX);
    dst.WriteStr(thetoken);
    dst.WriteStr("\", \"access_token\": \"" TOKEN_PREFIX);
    dst.WriteStr(thetoken);
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
    return 0;
}

int MxidHandler_v2::get_pubkey_isvalid(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return 0;
}

int MxidHandler_v2::get_pubkey(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return 0;
}

int MxidHandler_v2::get_hashdetails(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return 0;
}

int MxidHandler_v2::get_lookup(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return 0;
}

int MxidHandler_v2::get_validate_email_requestToken(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return 0;
}

int MxidHandler_v2::get_validate_email_submitToken(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return 0;
}

int MxidHandler_v2::post_validate_email_submitToken(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return 0;
}

int MxidHandler_v2::get_validate_msisdn_requestToken(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return 0;
}

int MxidHandler_v2::get_validate_msisdn_submitToken(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return 0;
}

int MxidHandler_v2::post_validate_msisdn_submitToken(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return 0;
}

int MxidHandler_v2::post_3pid_bind(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return 0;
}

int MxidHandler_v2::get_2pid_getValidated3pid(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return 0;
}

int MxidHandler_v2::post_3pid_unbind(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return 0;
}

int MxidHandler_v2::post_store_invite(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return 0;
}

int MxidHandler_v2::post_sign_ed25519(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const
{
    return 0;
}

