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
    ~ViewHandler();

    virtual int onRequest(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const override;

private:
    const DataTree& _tree;
    const view::Mgr& _vmgr;
};

