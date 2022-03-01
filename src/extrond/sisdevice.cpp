#include "sisdevice.h"
#include "viewexec.h"
#include "viewparser.h"
#include "json_out.h"
#include "util.h"

SISDeviceTemplate::SISDeviceTemplate(TreeMem& mem)
    : vw(mem)
{
}

bool SISDeviceTemplate::init(VarCRef devtype)
{
    bool ok = vw.load(devtype, false);
#ifdef _DEBUG
    std::vector<std::string> dis;
    vw.exe.disasm(dis);
    for(size_t i = 0; i < dis.size(); ++i)
        puts(dis[i].c_str());
#endif
    return ok;
}

SISDevice::SISDevice()
    : heartbeatTime(0)
{
}

bool SISDevice::init(const SISDeviceTemplate& dev, VarCRef unitcfg)
{
    Var v = dev.vw.produceResult(*this, unitcfg, VarCRef()); // no vars
    if(v.type() == Var::TYPE_NULL)
        return false;

    const VarCRef ref(this, &v);
    bool ok = _import(ref);
    if(ok)
    {
#ifdef _DEBUG
        std::string js = dumpjson(ref, true);
        printf("Device init ok, this is the JSON:\n%s\n", js.c_str());
#endif
    }
    else
    {
        std::string err = dumpjson(ref, true);
        printf("SISDevice::init(): Bad config. This is the failed JSON:\n%s\n", err.c_str());
    }

    v.clear(*this);
    return ok;
}


/*
const SISAction* SISDevice::getAction(const char* name) const
{
    auto it = actions.find(name);
    return it != actions.end() ? &it->second : NULL;
}
*/

bool SISDevice::_import(VarCRef ref)
{
    if(VarCRef xhb = ref.lookup("heartbeat_time"))
    {
        const char *shb = xhb.asCString();
        if(!strToDurationMS_Safe(&heartbeatTime, shb))
        {
            printf("SISDevice: Failed to parse heartbeat_time\n");
            return false;
        }
    }

    if (VarCRef xioy = ref.lookup("io_yield_time"))
    {
        const char* sioy = xioy.asCString();
        if (!strToDurationMS_Safe(&ioYieldTime, sioy))
        {
            printf("SISDevice: Failed to parse io_yield_time\n");
            return false;
        }
    }

    if(VarCRef xsc = ref.lookup("script"))
    {
        const char *ssc = xsc.asCString();
        if(!ssc)
        {
            printf("SISDevice: Property 'script' is not a string\n");
            return false;
        }
        script = ssc;
    }

    /*
    VarCRef xact = ref.lookup("actions");
    if(!xact || xact.type() != Var::TYPE_MAP)
        return false;

    const Var::Map *m = xact.v->map();
    for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
    {
        const char *k = getS(it.key());
        SISAction& sa = actions[k];
        if(!sa.parse(VarCRef(this, &it.value())))
        {
            actions.erase(k);
            return false;
        }
    }
    */
    return true;
}
