#pragma once

#include "datatree.h"
#include "webserver.h"
#include <vector>

class InfoHandler : public RequestHandler
{
public:
    InfoHandler(const DataTree& tree, const char* prefix);
    virtual ~InfoHandler();
    virtual int onRequest(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const override;
protected:
    const DataTree& _tree;
};

class DebugStrpoolHandler : public RequestHandler
{
public:
    DebugStrpoolHandler(const DataTree& tree, const char* prefix);
    virtual ~DebugStrpoolHandler();
    virtual int onRequest(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const override;
protected:
    const DataTree& _tree;
};

class DebugCleanupHandler : public RequestHandler
{
public:
    DebugCleanupHandler(DataTree& tree, const char* prefix);
    virtual ~DebugCleanupHandler();
    virtual int onRequest(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const override;
    void addForCleanup(RequestHandler* h) { _toclean.push_back(h); }
protected:
    DataTree& _tree;
    std::vector<RequestHandler*> _toclean;
};
