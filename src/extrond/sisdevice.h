#pragma once

#include "types.h"
#include <map>
#include <string>
#include "sisaction.h"
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
    const SISAction *getAction(const char *name) const;

private:
    std::map<std::string, SISAction> actions;
    u64 heartbeatTime;
    bool _import(VarCRef ref);
};
