#include <string.h>
#include "handler_view.h"
#include "view/viewmgr.h"
#include "view/viewexec.h"
#include "civetweb/civetweb.h"
#include "json_out.h"

ViewHandler::ViewHandler(const view::Mgr& mgr, const DataTree& tree, const char* prefix, const ServerConfig& cfg)
    : RequestHandler(prefix)
    , _tree(tree), _vmgr(mgr)
{
}

ViewHandler::~ViewHandler()
{
}

static void writeToStream(BufferedWriteStream& ws, VarCRef sub, const Request& r)
{
    writeJson(ws, sub, !!(r.flags & RQF_PRETTY));
}

int ViewHandler::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    view::VM vm;

    const char *qbeg = rq.query.c_str();
    if(*qbeg == '/')
        ++qbeg;
    const char *qend = strchr(qbeg, '/');
    if(!qend)
        qend = qbeg + rq.query.length();

    if(!_vmgr.initVM(vm, qbeg, qend - qbeg - 1))
    {
        mg_send_http_error(conn, 404, "");
        return 404;
    }

    bool ok;
    const char *err = "Error executing view";
    Var res;
    {
        // --- LOCK READ ---
        std::shared_lock<std::shared_mutex> lock(_tree.mutex);
        ok = vm.run(_tree.root());
        if(ok)
        {
            ok = vm.exportResult(res);
            err = "View returns more than one element. Consolidate into array or map. This is a server-side problem.";
        }
    }

    if(!ok)
    {
        mg_send_http_error(conn, 500, err);
        // TODO: dump disasm and state in debug mode
        return 500;
    }

    // res is a copy of the data, so we don't need a lock here anymore
    writeToStream(dst, VarCRef(vm, &res), rq);
    res.clear(vm);

    return 0;
}
