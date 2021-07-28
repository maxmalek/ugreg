#pragma once

#include <string>
#include <vector>

#include "variant.h"


struct ServerConfig
{
    struct Listen
    {
        std::string host;
        unsigned port;
        bool ssl;
    };

   // --- begin fields ---
    std::vector<Listen> listen;
    unsigned listen_threads;
    bool expose_debug_apis;
    // --- end fields ---


    ServerConfig();

    bool apply(VarCRef root); // Sets values to above fields and returns true if the config looks good
};
