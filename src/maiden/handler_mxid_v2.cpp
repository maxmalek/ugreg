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

// anything after /_matrix/identity/v2
const MxidHandler_v2::Endpoint MxidHandler_v2::s_endpoints[] =
{
    { RQ_GET,  "/account",                       &get_account  },
    { RQ_POST, "/account/logout",                &post_account_logout  },
    { RQ_POST, "/account/register",              &post_account_register  },
    { RQ_GET,  "/terms",                         &get_terms  },
    { RQ_POST, "/terms",                         &post_terms  },
    { RQ_GET,  "/",                              &get_status  }, // status check
    { RQ_GET,  "/pubkey/ephemeral/isvalid",      &get_pubkey_eph_isvalid  },
    { RQ_GET,  "/pubkey/isvalid",                &get_pubkey_isvalid  },
    { RQ_GET,  "/pubkey",                        &get_pubkey  }, // actually pubkey/{keyId}
    { RQ_GET,  "/hash_details",                  &get_hashdetails  },
    { RQ_POST, "/lookup",                        &get_lookup  },
    { RQ_POST, "/validate/email/requestToken",   &get_validate_email_requestToken  },
    { RQ_GET,  "/validate/email/submitToken",    &get_validate_email_submitToken  },
    { RQ_POST, "/validate/email/submitToken",    &post_validate_email_submitToken  },
    { RQ_POST, "/validate/msisdn/requestToken",  &get_validate_msisdn_requestToken  },
    { RQ_GET,  "/validate/msisdn/submitToken",   &get_validate_msisdn_submitToken  },
    { RQ_POST, "/validate/msisdn/submitToken",   &post_validate_msisdn_submitToken  },
    { RQ_POST, "/3pid/bind",                     &post_3pid_bind  },
    { RQ_GET,  "/3pid/getValidated3pid",         &get_2pid_getValidated3pid  },
    { RQ_POST, "/3pid/unbind",                   &post_3pid_unbind  },
    { RQ_POST, "/store-invite",                  &post_store_invite  },
    { RQ_POST, "/sign-ed25519",                  &post_sign_ed25519  },
    { RQ_UNKNOWN, NULL, NULL }
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
    mg_send_http_ok(conn, "application/json; charset=utf-8", 2);
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
    : RequestHandler(prefix, NULL)
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

    const Endpoint *ep = static_cast<const Endpoint*>(vp.asPtr());

    // TODO: this should be uncached
    return (this->*ep->func)(dst, conn, rq);
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
        while(isspace(*s++))
            ++ws;
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
    if(token && *token)
    {
        strncpy(dst, token, avail);
        return true;
    }
    return false;
}


MxidHandler_v2::AuthResult MxidHandler_v2::_authorize(mg_connection* conn, const Request& rq) const
{
    AuthResult res;
    res.err = M_OK;
    res.status = 0;
    if(!extractToken(res.token, sizeof(res.token), conn, rq))
    {
        res.err = M_MISSING_PARAMS;
        res.status = 401;
    }
    else
    {
        res.err = _store.authorize(res.token);
        if(res.err)
            res.status = 403;
    }
    if(res.err)
        sendError(conn, res.status, res.err);

    return res;
}

int MxidHandler_v2::get_account(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    const AuthResult auth = _authorize(conn, rq);
    if(auth.status)
        return auth.status;
    std::string acc = _store.getAccount(auth.token);
    if(acc.empty())
        return 500;

    dst.WriteStr("{\"user_id\": \"");
    dst.Write(acc.c_str(), acc.length());
    dst.WriteStr("\"}");
    return 0;
}

int MxidHandler_v2::post_account_logout(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    const AuthResult auth = _authorize(conn, rq);
    if (auth.status)
        return auth.status;

    _store.logout(auth.token);
    return _ok(conn);
}

static int HttpsGet(std::vector<char>& dst, const char *host, const char *what, int timeout)
{
    char errbuf[1024];
    int res = 0;
    std::ostringstream os;
    os << "GET " << what << " HTTP/1.1\r\n"
       << "Host: " << host << "\r\n"
       << "Connection: close\r\n"
       << "\r\n";
    if (mg_connection* c = mg_connect_client(host, 443, true, errbuf, sizeof(errbuf)))
    {
        std::string request = os.str();
        mg_write(c, request.c_str(), request.size());
        res = mg_get_response(c, errbuf, sizeof(errbuf), (int)timeout);
    }

    if(res == 200)
    {
        // TODO read conn
    }
}

int MxidHandler_v2::post_account_register(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
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

    // get matrix server name from host's .well-known
    std::string homeserver;
    unsigned port;
    MxStore::LookupResult res = _store.getCachedHomeserverForHost(sn, homeserver, port);
    if(res == MxStore::FAILED)
        return sendError(conn, 502, M_NOT_FOUND, "No homeserver exsits for this host (cached)");
    
    if(res != MxStore::VALID)
    {
        int resp = lookupHomeserverForHost(homeserver, sn, 5000, 1024*50); // TODO: make configurable
    }
    
    return 0;
}

int MxidHandler_v2::get_terms(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    // TODO: this should be handled in Lua?
    dst.WriteStr("{\"policies\":{}}");
    return 0;
}

int MxidHandler_v2::post_terms(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 501; // TODO
}

int MxidHandler_v2::get_status(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return _ok(conn);
}

int MxidHandler_v2::get_pubkey_eph_isvalid(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}

int MxidHandler_v2::get_pubkey_isvalid(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}

int MxidHandler_v2::get_pubkey(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}

int MxidHandler_v2::get_hashdetails(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}

int MxidHandler_v2::get_lookup(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}

int MxidHandler_v2::get_validate_email_requestToken(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}

int MxidHandler_v2::get_validate_email_submitToken(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}

int MxidHandler_v2::post_validate_email_submitToken(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}

int MxidHandler_v2::get_validate_msisdn_requestToken(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}

int MxidHandler_v2::get_validate_msisdn_submitToken(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}

int MxidHandler_v2::post_validate_msisdn_submitToken(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}

int MxidHandler_v2::post_3pid_bind(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}

int MxidHandler_v2::get_2pid_getValidated3pid(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}

int MxidHandler_v2::post_3pid_unbind(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}

int MxidHandler_v2::post_store_invite(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}

int MxidHandler_v2::post_sign_ed25519(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}
