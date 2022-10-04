#pragma once

#include <string>

class VarCRef;

struct URLTarget
{
    std::string host;
    unsigned port = 0;
    bool ssl = false;
    std::string path;

    bool load(const VarCRef& ref);
    bool parse(const char *url, unsigned defaultport = 0);
    inline bool isValid() const { return port && host.length(); }
};
