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
    u32 listen_threads;
    bool expose_debug_apis;
    struct
    {
        u32 rows, columns;
        u64 maxtime;
    } reply_cache;
    // --- end fields ---


    ServerConfig();

    bool apply(VarCRef root); // Apply values from root to above fields and returns true if the config looks good
};
