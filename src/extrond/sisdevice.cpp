#include "sisdevice.h"
#include "viewexec.h"
#include "viewparser.h"
#include "json_out.h"
#include "util.h"

SISDevice::SISDevice()
    : heartbeatTime(0), ioYieldTime(0)
{
}

bool SISDevice::init(VarCRef devcfg)
{
    bool ok = _import(devcfg);
    if(!ok)
    {
        std::string err = dumpjson(devcfg, true);
        printf("SISDevice::init(): Bad config. This is the failed JSON:\n%s\n", err.c_str());
    }
    return ok;
}

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

    return true;
}
