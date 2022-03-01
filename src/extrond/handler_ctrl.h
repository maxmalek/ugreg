#pragma once

#include "webserver.h"
#include <iosfwd>

struct ServerConfig;
class SISClient;
class ResponseFormatter;

class CtrlHandler : public RequestHandler
{
public:
    typedef std::vector<SISClient*> ClientList;

    CtrlHandler(const ClientList &clients, const char *prefix);
    virtual ~CtrlHandler();

    virtual int onRequest(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const override;

private:
    const ClientList& clients;
};
