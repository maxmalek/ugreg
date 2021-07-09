#pragma once

#include "datatree.h"

class TreeHandler
{
public:
    TreeHandler(size_t skipFromRequest);
    ~TreeHandler();
    static int Handler(struct mg_connection* conn, void* self);

private:
    int onRequest(struct mg_connection* conn);

    DataTree tree;
    size_t _skipFromRequest; // FIXME: this is ugly
};

