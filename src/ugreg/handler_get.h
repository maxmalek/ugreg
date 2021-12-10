#pragma once

#include "datatree.h"
#include "webserver.h"

struct ServerConfig;

// HTTP request handler for a tree. Must stay alive at least as long as the associated tree.
// Locks the tree for reading when accessed.
class TreeHandler : public RequestHandler
{
public:
    TreeHandler(DataTree &tree, const char *prefix, const ServerConfig& cfg);
    virtual ~TreeHandler();

    virtual int onRequest(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const override;

    DataTree& tree;

};

