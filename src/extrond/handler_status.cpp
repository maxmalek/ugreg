#include "handler_status.h"
#include <sstream>
#include <stdio.h>
#include "civetweb/civetweb.h"
#include "json_out.h"
#include "jsonstreamwrapper.h"
#include "treefunc.h"
#include "config.h"
#include "sisclient.h"
#include "responseformat.h"

StatusHandler::StatusHandler(const ClientList& clients, const char *prefix)
    : RequestHandler(prefix, "application/json"), clients(clients)
{
}

StatusHandler::~StatusHandler()
{
}

static void writeToStream(BufferedWriteStream & ws, VarCRef sub, const Request & r)
{
    writeJson(ws, sub, !!(r.flags & RQF_PRETTY));
}

void StatusHandler::prepareClientList(ResponseFormatter& fmt) const
{
    fmt.addHeader("name", "Name");
    fmt.addHeader("host", "Host");
    fmt.addHeader("port", "Port");
    fmt.addHeader("cstate", "Connection state");
    fmt.addHeader("cstateTime", "Time in state");
    fmt.addHeader("status", "Device status");
    fmt.addHeader("link", "Link");

    const size_t N = clients.size();
    for(size_t i = 0; i < N; ++i)
    {
        VarRef t = fmt.next();
        SISClient *cl = clients[i];
        const SISClientConfig& c = cl->getConfig();
        t["name"] = c.name.c_str();
        t["host"] = c.host.c_str();
        t["port"] = (u64)c.port;
        t["cstate"] = cl->getStateStr();
        t["cstateTime"] = cl->getTimeInState();
        t["status"] = cl->askStatus().c_str();

        std::ostringstream os;
        os << "<a href=\"/ctrl/" << c.name << "\">Go</a>";
        t["link"] = os.str().c_str();
    }
}

// This is called from many threads at once.
// Avoid anything that changes the tree, and ensure proper read-locking!
int StatusHandler::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    const char* q = rq.query.c_str();
    if(q && strchr(q, '/'))
    {
        mg_send_http_redirect(conn, this->prefix(), 301);
        return 301;
    }

    ResponseFormatter fmt;
    prepareClientList(fmt);

    if(rq.fmt == RQFMT_JSON)
    {
        fmt.emitJSON(dst, !!(rq.flags & RQF_PRETTY));
        return 0;
    }

    std::ostringstream os;
    os << "<html><body>";
    os << "(This page is also available as <a href=\"?json\">JSON</a>)<br />\n";
    os << clients.size() << " clients configured:<br />";
    fmt.emitHTML(os);
    os << "</body></html>";
    std::string tmp = os.str();
    const size_t N = tmp.length();
    mg_send_http_ok(conn, "text/html; charset=utf-8", tmp.length());
    mg_write(conn, tmp.c_str(), tmp.length());

    return 200;
}
