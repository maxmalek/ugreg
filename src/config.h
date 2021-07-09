#pragma once

#include <string>

struct ServerConfig
{
    ServerConfig();
    std::string listenaddr;
    unsigned listenport;
};
