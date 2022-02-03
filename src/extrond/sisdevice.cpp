#include "sisdevice.h"
#include "viewexec.h"
#include "viewparser.h"
#include "sisaction.h"
#include "json_out.h"

bool SISDeviceTemplate::init(VarCRef devtype)
{
    return vw.load(devtype, false);
}

SISDevice::SISDevice()
    : heartbeatTime(0)
{
}

bool SISDevice::init(const SISDeviceTemplate& dev, VarCRef devcfg)
{
    Var v = dev.vw.produceResult(*this, devcfg, VarCRef()); // no vars
    if(v.type() == Var::TYPE_NULL)
        return false;

    const VarCRef ref(this, &v);
    bool ok = _import(ref);
    if(!ok)
    {
        std::string err = dumpjson(ref, true);
        printf("SISDevice::init(): Bad config. This is the failed JSON:\n%s\n", err.c_str());
    }

    v.clear(*this);
    return ok;
}

bool SISDevice::_import(VarCRef ref)
{
    VarCRef xhb = ref.lookup("heartbeat_time");
    heartbeatTime = xhb && xhb.asUint() ? *xhb.asUint() : 0;

    VarCRef xact = ref.lookup("actions");
    if(!xact || xact.type() != Var::TYPE_MAP)
        return false;

    const Var::Map *m = xact.v->map();
    for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
    {
        const char *k = getS(it.key());
        SISAction& sa = actions[k];
        if(!sa.parse(VarCRef(this, &it.value())))
            return false;
    }

    return true;
}
