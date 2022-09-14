#pragma once

#include "webserver.h"
#include "datatree.h"
#include "mxstore.h"

class MxWellknownHandler : public RequestHandler
{
public:
    MxWellknownHandler(VarCRef data);
    virtual int onRequest(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const override;

private:
    std::string client, server;
};

class MxSearchHandler : public RequestHandler
{
public:
    MxSearchHandler(MxStore& store, VarCRef cfg);
    virtual int onRequest(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const override;

    MxStore& _store;
    MxStore::SearchConfig searchcfg;
};

class MxReverseProxyHandler : public RequestHandler
{
    MxReverseProxyHandler();
    virtual int onRequest(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const override;

};
