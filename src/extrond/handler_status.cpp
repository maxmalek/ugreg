#include "handler_status.h"
#include <sstream>
#include <stdio.h>
#include "civetweb/civetweb.h"
#include "json_out.h"
#include "jsonstreamwrapper.h"
#include "treefunc.h"
#include "config.h"
#include "sisclient.h"

#define BR "<br />\n"


StatusHandler::StatusHandler(const ClientList& clients, const char *prefix)
    : RequestHandler(prefix, "text/html; charset=utf-8"), clients(clients)
{
}

StatusHandler::~StatusHandler()
{
}

static void writeToStream(BufferedWriteStream & ws, VarCRef sub, const Request & r)
{
    writeJson(ws, sub, !!(r.flags & RQF_PRETTY));
}

void StatusHandler::emitClientList(std::ostringstream& os) const
{
    const size_t N = clients.size();
    os << N << " clients configured:" BR;
    os << "<table border=\"1\">\n";
    os << "<tr>";
    os << "<th>Name</td>";
    os << "<th>Host</th>";
    os << "<th>Telnet port</th>";
    os << "<th>Connection state</th>";
    os << "<th>Time in state</th>";
    os << "<th>Device state</th>";
    os << "<tr/>";
    for(size_t i = 0; i < N; ++i)
    {
        os << "<tr>";
        emitOneClient(os, clients[i]);
        os << "</tr>\n";
    }
    os << "</table>";
}

void StatusHandler::emitOneClient(std::ostringstream& os, const SISClient *cl) const
{
    const SISClientConfig& c = cl->getConfig();
    os << "<td>" << c.name << "</td>";
    os << "<td><a href=\"http://" << c.host << "\">" << c.host << "</td>";
    os << "<td>" << c.port << "</td>";
    os << "<td>" << cl->getStateStr() << "</td>";
    os << "<td>" << cl->getTimeInState() << "</td>";
    os << "<td>" << cl->askStatus() << "</td>";
}

// This is called from many threads at once.
// Avoid anything that changes the tree, and ensure proper read-locking!
int StatusHandler::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    const char *who = rq.query.c_str();

    /*if(!sub)
    {
        mg_send_http_error(conn, 404, "");
        return 404;
    }*/
    //writeToStream(dst, sub, rq);

    std::ostringstream os;
    os << "<html><body>";
    emitClientList(os);
    os << "</body></html>";
    std::string tmp = os.str();
    const size_t N = tmp.length();
    /*dst.Write(tmp.c_str(), tmp.length());
    dst.Flush();*/
    //mg_send_chunk(conn, tmp.c_str(), (unsigned)tmp.length());
    //mg_send_chunk(conn, "", 0);
    mg_send_http_ok(conn, "text/html", tmp.length());
    mg_write(conn, tmp.c_str(), tmp.length());

    return 200;
}
