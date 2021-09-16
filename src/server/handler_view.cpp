#include <string.h>
#include "handler_view.h"
#include "view/viewmgr.h"
#include "view/viewexec.h"
#include "view/viewparser.h"
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
            // Until after the export is done, the VM may still hold refs
            // to the tree, so we need to keep the lock held until then.
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


ViewDebugHandler::ViewDebugHandler(const DataTree& tree, const char* prefix, const ServerConfig& cfg)
    : RequestHandler(prefix)
    , _tree(tree)
{
}

ViewDebugHandler::~ViewDebugHandler()
{
}

static void writeStr(BufferedWriteStream& out, const char *s)
{
    while(*s)
        out.Put(*s++);
}

int ViewDebugHandler::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    const char *query = rq.query.c_str();
    printf("ViewDebugHandler: %s\n", query);
    view::VM vm;
    view::Executable exe(vm);
    std::string err;
    size_t start = view::parse(exe, query, err);
    if (!start)
    {
        mg_send_http_error(conn, 400, "Query parse error\n");
        writeStr(dst, err.c_str());
        return 400;
    }

    {
        writeStr(dst, "--- Disasm ---\n");
        std::vector<std::string> dis;
        exe.disasm(dis);
        for (size_t i = 1; i < dis.size(); ++i)
        {
            writeStr(dst, dis[i].c_str());
            dst.Put('\n');
        }
    }

    vm.init(exe, NULL, 0);

    // --- LOCK READ ---
    std::shared_lock<std::shared_mutex> lock(_tree.mutex);

    if(!vm.run(_tree.root()))
    {
        mg_send_http_error(conn, 500, "VM run failed\n");
        // TODO: dump state
        return 500;
    }

    // out can still contain refs directly into the tree, so
    // we need to keep the lock enabled for now.
    const view::VarRefs& out = vm.results();

    dst.Put('\n');
    {
        char buf[64];
        sprintf(buf, "--- Results: %u ---\n", (unsigned)out.size());
        writeStr(dst, buf);
        if(out.size() > 0)
            writeStr(dst, "--- WARNING: Reduce the number of results to 1 for live data, else this will fail!\n");

    }
    dst.Put('\n');

    for (const view::VarEntry& e : out)
    {
        dst.Put('<');
        const char *k = vm.getS(e.key);
        writeStr(dst, k ? k : "(no key name)");
        dst.Put('>');
        dst.Put('\n');

        writeStr(dst, dumpjson(e.ref, true).c_str());
        dst.Put('\n');
    }

    return 0;
}
