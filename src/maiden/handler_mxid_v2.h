#pragma once

#include "webserver.h"
#include "datatree.h"
#include "mxdefines.h"

class MxStore;

class MxidHandler_v2 : public RequestHandler
{
public:
    MxidHandler_v2(MxStore& mxs, const char* prefix);
    virtual ~MxidHandler_v2();
    virtual int onRequest(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const override;

private:
    MxStore& _store;
    DataTree _handlers; // ptr entries are actually Endpoint*
    unsigned _port;
    void _setupHandlers();

    typedef int (MxidHandler_v2::*EndpointFunc)(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;

    struct Endpoint
    {
        RequestType type;
        const char* endpoint;
        EndpointFunc func;
    };

    static const Endpoint s_endpoints[];

    struct AuthResult
    {
        int status;
        MxError err;
        char token[256];
    };

    // 0 if ok, http error code otherwise
    AuthResult _authorize(mg_connection* conn, const Request& rq) const;

    int get_account                      (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
    int post_account_logout              (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
    int post_account_register            (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
    int get_terms                        (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
    int post_terms                       (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
    int get_status                       (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
    int get_pubkey_eph_isvalid           (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
    int get_pubkey_isvalid               (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
    int get_pubkey                       (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
    int get_hashdetails                  (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
    int get_lookup                       (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
    int get_validate_email_requestToken  (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
    int get_validate_email_submitToken   (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
    int post_validate_email_submitToken  (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
    int get_validate_msisdn_requestToken (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
    int get_validate_msisdn_submitToken  (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
    int post_validate_msisdn_submitToken (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
    int post_3pid_bind                   (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
    int get_2pid_getValidated3pid        (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
    int post_3pid_unbind                 (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
    int post_store_invite                (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
    int post_sign_ed25519                (BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const;
};
