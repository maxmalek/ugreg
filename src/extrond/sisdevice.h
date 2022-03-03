#pragma once

#include "types.h"
#include <map>
#include <string>
#include "view.h"
#include "datatree.h"

// Init based on a template with an actual physical device
// to get its config
class SISDevice
{
public:
    SISDevice();
    bool init(VarCRef devcfg);

    u64 getHeartbeatTime() const { return heartbeatTime; }
    u64 getIOYieldTime() const { return ioYieldTime; }
    u64 getHttpTimeout() const { return httpTimeout; }
    const char *getScript() const { return script.c_str(); }

private:
    u64 heartbeatTime;
    u64 ioYieldTime;
    u64 httpTimeout;
    std::string script;
    bool _import(VarCRef ref);
};
