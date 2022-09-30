#pragma once

#include <string>
#include <vector>

#include "types.h"
#include "webstuff.h"


struct ServerConfig
{
    typedef URLTarget Listen;

   // --- begin fields ---
    std::vector<Listen> listen;
    std::string cert;
    u32 listen_threads;
    bool expose_debug_apis;
    struct
    {
        u32 rows, columns;
        u64 maxtime;
    } reply_cache;
    std::string mimetype;
    // --- end fields ---


    ServerConfig();

    bool apply(const VarCRef& root); // Apply values from root to above fields and returns true if the config looks good
};
