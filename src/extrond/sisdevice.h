#pragma once

#include "types.h"
#include <map>
#include <string>
#include "view.h"
#include "datatree.h"

// One config per device TYPE
class SISDeviceTemplate
{
public:
    SISDeviceTemplate(TreeMem& mem);

    // Just load and interpolate strings
    bool init(VarCRef devtype);

    view::View vw;
};

// Init based on a template with an actual physical device
// to get its config
class SISDevice : public DataTree
{
public:
    SISDevice();
    bool init(const SISDeviceTemplate& dev, VarCRef unitcfg);

    //std::map<std::string, std::string> errors;
    u64 getHeartbeatTime() const { return heartbeatTime; }
    u64 getIOYieldTime() const { return ioYieldTime; }
    const char *getScript() const { return script.c_str(); }

private:
    u64 heartbeatTime;
    u64 ioYieldTime;
    std::string script;
    bool _import(VarCRef ref);
};
