#include "sisdevice.h"
#include "viewexec.h"
#include "viewparser.h"
#include "json_out.h"
#include "util.h"

SISDevice::SISDevice()
    : heartbeatTime(0), ioYieldTime(0), httpTimeout(0)
{
}

static bool readtime(u64& t, VarCRef mapref, const char *key, bool missing)
{
    if (VarCRef x = mapref.lookup(key))
    {
        if (!strToDurationMS_Safe(&t, x.asCString()))
        {
            logerror("SISDevice: Failed to parse %s", key);
            return false;
        }
    }
    else
    {
        if(!missing)
            logerror("SISDevice: Key '%s' not present (need a duration)", key);
        return missing;
    }

    return true;
}

bool SISDevice::init(VarCRef devcfg)
{
    bool ok = _import(devcfg);
    if(!ok)
    {
        std::string err = dumpjson(devcfg, true);
        logerror("SISDevice::init(): Bad config. This is the failed JSON:\n%s", err.c_str());
    }
    return ok;
}

bool SISDevice::_import(VarCRef ref)
{
    bool ok = true;
    ok = readtime(heartbeatTime, ref, "heartbeat_time", true) && ok;
    ok = readtime(ioYieldTime, ref, "io_yield_time", true) && ok;
    ok = readtime(httpTimeout, ref, "http_timeout", true) && ok;

    if(VarCRef xsc = ref.lookup("script"))
    {
        const char *ssc = xsc.asCString();
        if(!ssc)
        {
            logerror("SISDevice: Property 'script' is not a string");
            return false;
        }
        script = ssc;
    }
    else
    {
        logerror("SISDevice: Need 'script', can't do anything without it");
        return false;
    }

    return true;
}
