#pragma once

#include "webserver.h"
#include "datatree.h"
#include "mxdefines.h"

class MxStore;

class MxidHandler_v2 : public RequestHandler
{
public:
    MxidHandler_v2(MxStore& mxs, const char* prefix);
    bool init(VarCRef cfg);
    virtual ~MxidHandler_v2();
    virtual int onRequest(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const override;
private:
    MxStore& _store;
    DataTree _handlers; // ptr entries are actually Endpoint*
    unsigned _port;
    void _setupHandlers();

    enum AuthStatus
    {
        NOAUTH,
        AUTHED,
    };

    // Infos about the currently authorized user that sent the request (previously via /account/register)
    struct UserInfo
    {
        AuthStatus auth = NOAUTH;
        std::string username, token;
    };

    typedef int (MxidHandler_v2::*EndpointFunc)(BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;

    struct Endpoint
    {
        RequestType type;
        AuthStatus auth;
        const char* endpoint;
        EndpointFunc func;
    };

    static const Endpoint s_endpoints[];

    struct AuthResult
    {
        int httpstatus; // http status if there was an error, otherwise 0
        MxError err;
        char token[256];
    };


    AuthResult _tryAuthorize(mg_connection* conn, const Request& rq) const;

    int get_account                      (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;
    int post_account_logout              (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;
    int post_account_register            (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;
    int get_terms                        (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;
    int post_terms                       (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;
    int get_status                       (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;
    int get_pubkey_eph_isvalid           (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;
    int get_pubkey_isvalid               (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;
    int get_pubkey                       (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;
    int get_hashdetails                  (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;
    int get_lookup                       (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;
    int get_validate_email_requestToken  (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;
    int get_validate_email_submitToken   (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;
    int post_validate_email_submitToken  (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;
    int get_validate_msisdn_requestToken (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;
    int get_validate_msisdn_submitToken  (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;
    int post_validate_msisdn_submitToken (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;
    int post_3pid_bind                   (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;
    int get_2pid_getValidated3pid        (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;
    int post_3pid_unbind                 (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;
    int post_store_invite                (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;
    int post_sign_ed25519                (BufferedWriteStream& dst, mg_connection* conn, const Request& rq, const UserInfo& u) const;

public:
    bool fake_v1;
};
