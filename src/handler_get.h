#pragma once

#include "datatree.h"

// HTTP request handler for a tree. Must stay alive at least as long as the associated tree.
class TreeHandler
{
public:
    TreeHandler(DataTree &tree, size_t skipFromRequest);
    ~TreeHandler();
    static int Handler(struct mg_connection* conn, void* self);

private:
    int onRequest(struct mg_connection* conn);

    DataTree& tree;
    size_t _skipFromRequest; // FIXME: this is ugly
};

