#pragma once

#include <string>

class VarCRef;

struct URLTarget
{
    std::string host;
    unsigned port;
    bool ssl;
    std::string path;

    bool load(const VarCRef& ref);
    bool parse(const char *url, unsigned defaultport = 0);
};
