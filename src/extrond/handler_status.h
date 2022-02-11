#pragma once

#include "webserver.h"
#include <iosfwd>

struct ServerConfig;
class SISClient;

// HTTP request handler for a tree. Must stay alive at least as long as the associated tree.
// Locks the tree for reading when accessed.
class StatusHandler : public RequestHandler
{
public:
    typedef std::vector<SISClient*> ClientList;

    StatusHandler(const ClientList &clients, const char *prefix);
    virtual ~StatusHandler();

    virtual int onRequest(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const override;

private:
    const ClientList& clients;
    void emitClientList(std::ostringstream& os) const;
    void emitOneClient(std::ostringstream& os, const SISClient *cl) const;

};
