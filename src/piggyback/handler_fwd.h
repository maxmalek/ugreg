#pragma once

#include "webserver.h"
#include <string>
#include "variant.h"

struct ServerConfig;
class SISClient;
class ResponseFormatter;

class PiggybackHandler : public RequestHandler
{
public:
    PiggybackHandler();
    bool apply(VarCRef cfg);
    virtual ~PiggybackHandler();

    virtual int onRequest(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const override;

    enum SSLStatus
    {
        DISABLED, ENABLED, AUTO
    };
    struct Destination
    {
        std::string host;
        unsigned port;
        bool fixHost;
        SSLStatus ssl;
    };

private:

    mg_connection *connectToDest(const Destination *& dst, const mg_request_info *info) const;

    std::vector<Destination> dest;
};
