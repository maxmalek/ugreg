#include "handler_mxid_v2.h"
#include <sstream>
#include <string>
#include "debugfunc.h"
#include "json_out.h"
#include "mxstore.h"
#include "request.h"
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

MxidHandler_v2::MxidHandler_v2(MxStore& mxs, const char* prefix)
    : RequestHandler(prefix, NULL)
    , _store(mxs)
    , _handlers(DataTree::TINY)
{
    _setupHandlers();
}

MxidHandler_v2::~MxidHandler_v2()
{
}

int MxidHandler_v2::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    VarCRef ref = _handlers.subtreeConst(rq.query.c_str());
    if(ref.type() != Var::TYPE_MAP)
        return 404;

    const char *mp = mountpointForRequestType(rq.type);
    if(!mp)
        return 405;

    VarCRef vp = ref.lookup(mp);
    if(!vp || vp.type() != Var::TYPE_PTR)
        return 405;

    const Endpoint *ep = static_cast<const Endpoint*>(vp.asPtr());
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

int MxidHandler_v2::get_account(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}

int MxidHandler_v2::post_account_logout(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}

int MxidHandler_v2::post_account_register(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}

int MxidHandler_v2::get_terms(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}

int MxidHandler_v2::post_terms(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    return 0;
}

int MxidHandler_v2::get_status(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    dst.Write("{}", 2);
    return 0;
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
