#pragma once

#include "webserver.h"
#include <iosfwd>

struct ServerConfig;
class SISClient;
class ResponseFormatter;

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
    void prepareClientList(ResponseFormatter& fmt) const;

};
