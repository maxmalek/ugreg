#pragma once

#include <string>
#include "sissocket.h"

struct SISClientConfig
{
    std::string name;
    std::string type;
    std::string host;
    unsigned port;
};

class SISClient
{
    SISSocket socket;
};
