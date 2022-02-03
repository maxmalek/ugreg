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
    bool init(const SISDeviceTemplate& dev, VarCRef devcfg);

    //std::map<std::string, std::string> errors;
    std::map<std::string, SISAction> actions;
    u64 heartbeatTime;

private:
    bool _import(VarCRef ref);
};
