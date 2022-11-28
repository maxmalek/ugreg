#pragma once

#include <map>
#include "webserver.h"
#include "datatree.h"
#include "mxstore.h"
#include "mxsearch.h"
#include "webstuff.h"

class MxSources;


class MxWellknownHandler : public RequestHandler
{
public:
    MxWellknownHandler(VarCRef data);
    virtual int onRequest(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const override;

private:
    std::map<std::string, std::string> data;
};

class MxReverseProxyHandler : public RequestHandler
{
public:
    MxReverseProxyHandler(VarCRef cfg);
    virtual int onRequest(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const override;

protected:
    URLTarget homeserver;
};


class MxSearchHandler : public MxReverseProxyHandler
{
public:
    MxSearchHandler(MxStore& store, VarCRef cfg, MxSources& sources);
    virtual ~MxSearchHandler();
    virtual int onRequest(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const override;

    MxStore& _store;
    bool reverseproxy;
    bool checkHS;
    bool askHS;
    int hsTimeout;
    MxSearchConfig searchcfg;
    MxSearch search;

protected:
    void doSearch(VarRef dst, const char* term, size_t limit) const;

private:
    MxSources& _sources;
};

