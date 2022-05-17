#include "handler_ctrl.h"
#include <sstream>
#include <stdio.h>
#include "civetweb/civetweb.h"
#include "json_out.h"
#include "json_in.h"
#include "jsonstreamwrapper.h"
#include "treefunc.h"
#include "config.h"
#include "sisclient.h"
#include "responseformat.h"

#define BR "<br />\n"


CtrlHandler::CtrlHandler(const ClientList& clients, const char *prefix)
    : RequestHandler(prefix, "application/json"), clients(clients)
{
}

CtrlHandler::~CtrlHandler()
{
}

static void writeToStream(BufferedWriteStream & ws, VarCRef sub, const Request & r)
{
    writeJson(ws, sub, !!(r.flags & RQF_PRETTY));
}


// This is called from many threads at once.
// Avoid anything that changes the tree, and ensure proper read-locking!
int CtrlHandler::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    const char *q = rq.query.c_str();

    if(q)
    {
        while(*q == '/')
            ++q;
        const char *action = strchr(q, '/');
        SISClient *cl = NULL;
        const size_t namelen = action ? action - q : strlen(q);
        for(size_t i = 0; i < clients.size(); ++i)
            if(!strncmp(clients[i]->getConfig().name.c_str(), q, namelen))
            {
                cl = clients[i];
                break;
            }
        if(!cl)
            return 404;
        if(cl->getState() < SISClient::IDLE)
        {
            mg_send_http_error(conn, 503, "Not yet connected to device, wait a little...");
            return 503;
        }

        if(action && *action)
            ++action; // skip '/'
        else
        {
            mg_send_http_redirect(conn, (std::string(q) + '/').c_str(), 301);
            return 301;
        }

        if(!(action && *action))
            action = "detail";


        DataTree params(DataTree::TINY);
        const Var *vp = NULL;
        if(Request::AutoReadVars(params.root(), conn) > 0)
            vp = params.root().v; // only try to pass vars to Lua when we actually get some vars in the query string

        SISClient::ActionResult res = cl->queryAsync(action, VarCRef(params, vp), cl->getConfig().device.getHttpTimeout()).get();
        if(res.error || res.status >= 400)
        {
            if(!res.status)
                res.status = 500;
            mg_send_http_error(conn, res.status, "%s", res.text.c_str());
            return res.status;
        }

        mg_send_http_ok(conn, res.contentType.length() ? res.contentType.c_str() : "text/plain; charset=utf-8", res.text.length());
        mg_write(conn, res.text.c_str(), res.text.length());
        return res.status ? res.status : 200;
    }

    return 404;
}
