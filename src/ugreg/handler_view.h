#pragma once

#include "datatree.h"
#include "webserver.h"

struct ServerConfig;
namespace view { class Mgr; }

// HTTP request handler for a view.
class ViewHandler : public RequestHandler
{
public:
    ViewHandler(const view::Mgr& mgr, const DataTree& tree, const char* prefix, const ServerConfig& cfg);
    virtual ~ViewHandler();

    virtual int onRequest(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const override;

protected:
    const DataTree& _tree;
    const view::Mgr& _vmgr;
};

class ViewDebugHandler : public RequestHandler
{
public:
    ViewDebugHandler(const DataTree& tree, const char* prefix, const ServerConfig& cfg);
    virtual ~ViewDebugHandler();
    virtual int onRequest(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const override;
protected:
    const DataTree& _tree;
};
