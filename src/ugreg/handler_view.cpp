#include <string.h>
#include <sstream>
#include "handler_view.h"
#include "viewmgr.h"
#include "viewexec.h"
#include "viewparser.h"
#include "civetweb/civetweb.h"
#include "json_out.h"
#include "debugfunc.h"
#include "pathiter.h"
#include "config.h"

ViewHandler::ViewHandler(const view::Mgr& mgr, const DataTree& tree, const char* prefix, const ServerConfig& cfg)
    : RequestHandler(prefix, cfg.mimetype.c_str())
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
    TreeMem work;
    view::VM vm(work);

    PathIter it(rq.query.c_str());
    PoolStr part = it.value();

    if(!_vmgr.initVM(vm, part.s, part.len))
    {
        mg_send_http_error(conn, 404, "");
        return 404;
    }

    // TODO: use it.remain() and make that acessible in the view

    bool ok;
    const char *err = "Error executing view";
    Var res;
    {
        // --- LOCK READ ---
        std::shared_lock lock(_tree.mutex);
        ok = vm.run(_tree.root());
        if(ok)
        {
            // Until after the export is done, the VM may still hold refs
            // to the tree, so we need to keep the lock held until then.
            ok = false;
            err = "View returns more than one element. Consolidate into array or map. This is a server-side problem.";
        }
    }

    if(!ok)
    {
        mg_send_http_error(conn, 500, "%s", err);
        // TODO: dump disasm and state in debug mode
        return 500;
    }

    // res is a copy of the data, so we don't need a lock here anymore
    writeToStream(dst, VarCRef(vm.mem, &res), rq);
    res.clear(vm.mem);

    return 0;
}


ViewDebugHandler::ViewDebugHandler(const DataTree& tree, const char* prefix, const ServerConfig& cfg)
    : RequestHandler(prefix, NULL)
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
    /*std::ostringstream qs;
    qs << '{' << query << '}';
    std::string qtmp = qs.str();
    query = qtmp.c_str();*/
    if(*query == '/')
        ++query;

    printf("ViewDebugHandler: %s\n", query);
    TreeMem work;
    view::VM vm(work);
    view::Executable exe(work);
    std::string err;
    size_t start = view::parse(exe, query, err);
    if (!start)
    {
        std::ostringstream os;
        os << "Query parse error:\n" << err;
        mg_send_http_error(conn, 400, "%s", os.str().c_str());
        return 400;
    }

    {
        writeStr(dst, query);
        writeStr(dst, "\n\n--- Disasm ---\n");
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
    std::shared_lock lock(_tree.mutex);

    if(!vm.run(_tree.root()))
    {
        writeStr(dst, "!!! Error during VM run. This is bad.\n");
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
        if(out.size() > 1)
            writeStr(dst, "--- WARNING: Reduce the number of results to 1 for live data, else this will fail!\n");

    }
    dst.Put('\n');

    for (const view::VarEntry& e : out)
    {
        dst.Put('<');
        const char *k = vm.mem.getS(e.key);
        writeStr(dst, k ? k : "(no key name)");
        dst.Put('>');
        dst.Put('\n');

        writeStr(dst, dumpjson(e.ref, true).c_str());
        dst.Put('\n');
    }

    writeStr(dst, "\n\n--- memory stats of VM after exec ---\n");
    std::ostringstream os;
    dumpAllocInfoToString(os, vm.mem);
    writeStr(dst, os.str().c_str());

    return 0;
}

