#pragma once

#include <string>
#include "variant.h"


struct ServerConfig
{
    struct Listen
    {
        std::string host;
        unsigned port;
        bool ssl;
    };

    ServerConfig();

    std::vector<Listen> listen;

    void clear();
    void add(VarCRef root);
    bool valid() const;
};
