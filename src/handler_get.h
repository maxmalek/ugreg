#pragma once

#include "datatree.h"
#include "request.h"
#include "cachetable.h"
#include "request.h"

struct ServerConfig;

// HTTP request handler for a tree. Must stay alive at least as long as the associated tree.
class TreeHandler
{
public:
    TreeHandler(DataTree &tree, size_t skipFromRequest, const ServerConfig& cfg);
    ~TreeHandler();
    static int Handler(struct mg_connection* conn, void* self);

    typedef CacheTable<Request, const StoredReply> Cache;

private:
    int onRequest(struct mg_connection* conn);

    DataTree& tree;
    size_t _skipFromRequest; // FIXME: this is ugly

    Cache _cache;
    const ServerConfig& cfg;
};

