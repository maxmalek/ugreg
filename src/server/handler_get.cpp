#include "handler_get.h"
#include <sstream>
#include <stdio.h>
#include "civetweb/civetweb.h"
#include "json_out.h"
#include "jsonstreamwrapper.h"
#include "treefunc.h"
#include "config.h"


TreeHandler::TreeHandler(const DataTree& tree, const char *prefix, const ServerConfig& cfg )
    : RequestHandler(prefix), tree(tree)
{
}

TreeHandler::~TreeHandler()
{
}

static void writeToStream(BufferedWriteStream & ws, VarCRef sub, const Request & r)
{
    writeJson(ws, sub, !!(r.flags & RQF_PRETTY));
}

// This is called from many threads at once.
// Avoid anything that changes the tree, and ensure proper read-locking!
int TreeHandler::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    // ---- BEGIN READ LOCK ----
    // Tree query and use of the result must be locked.
    // Can't risk a merge or expire process to drop parts of the tree in another thread that we're still using here.
    std::shared_lock<std::shared_mutex> lock(tree.mutex);

    VarCRef sub = tree.subtree(rq.query.c_str());

    //printf("sub = %p\n", sub.v);
    if(!sub)
    {
        mg_send_http_error(conn, 404, "");
        return 404;
    }

    writeToStream(dst, sub, rq);
    return 0;
}
