#include "mxstore.h"
#include "util.h"

// Yes, the entries in authdata are stringly typed.
// But this way saving/restoring the entire structure is very simple if we ever need it
// (just dump as json, then load later)

static const u64 second = 1000;
static const u64 minute = second * 60;
static const u64 hour = minute * 60;

MxStore::MxStore()
    : authdata(DataTree::SMALL), wellknown(DataTree::SMALL)
    , _wellKnownValidTime(1 * hour)
    , _wellKnownFailTime(10 * minute)
{
    authdata.root().makeMap();
}

void MxStore::register_(const char* token, size_t expireInSeconds, const char *account)
{
    std::lock_guard lock(authdata.mutex);
    u64 expiryTime = timeNowMS() + expireInSeconds * 1000;
    VarRef u = authdata.root()[token];
    u["expiry"] = expiryTime;
    u["account"] = account;
}

MxError MxStore::authorize(const char* token) const
{
    std::lock_guard lock(authdata.mutex);
    VarCRef ref = authdata.root().lookup(token);
    if(!ref)
        return M_NOT_FOUND;
    VarCRef exp = ref.lookup("expiry");
    if(!exp)
        return M_SESSION_EXPIRED;
    const u64 *pexpiryTime = exp.asUint();
    if(!pexpiryTime || *pexpiryTime < timeNowMS())
        return M_SESSION_EXPIRED;

    return M_OK;
}

std::string MxStore::getAccount(const char* token) const
{
    std::string ret;
    std::lock_guard lock(authdata.mutex);
    VarCRef ref = authdata.root().lookup(token);
    if (ref)
        if(VarCRef acc = ref.lookup("account"))
            if(const char *pacc = acc.asCString())
                ret = pacc;
    return ret;
}

void MxStore::logout(const char* token)
{
    std::lock_guard lock(authdata.mutex);
    VarRef ref = authdata.root().lookup(token);
    ref.clear();
}

void MxStore::storeHomeserverForHost(const char* host, const char* hs, unsigned port)
{
    assert(hs && *hs && port);
    std::lock_guard lock(wellknown.mutex);
    VarRef u = wellknown.root()[host];
    u["homeserver"] = hs;
    u["port"] = u64(port);
    u["ts"] = timeNowMS();
}

void MxStore::storeFailForHost(const char* host)
{
    // FIXME: might want to use a CacheTable<> instead
    std::lock_guard lock(wellknown.mutex);
    wellknown.root()[host] = timeNowMS();
}

MxStore::LookupResult MxStore::getCachedHomeserverForHost(const char* host, std::string& hsOut, unsigned& portOut) const
{
    u64 ts;
    {
        std::lock_guard lock(wellknown.mutex);
        VarCRef u = wellknown.root().lookup(host);
        if(!u)
            return UNKNOWN;

        if(const u64 *pFailTs = u.asUint())
        {
            return *pFailTs + _wellKnownFailTime < timeNowMS()
                ? UNKNOWN // fail expired, time to try again
                : FAILED;
        }
        VarCRef hsRef = u.lookup("homeserver");
        VarCRef portRef = u.lookup("port");
        VarCRef tsRef = u.lookup("ts");
        if(!(hsRef && portRef && tsRef))
            return UNKNOWN;
        const char *hs = hsRef.asCString();
        const u64 *pport = portRef.asUint();
        const u64 *pts = tsRef.asUint();
        if(!(hs && pport && pts))
            return UNKNOWN;

        hsOut = hs;
        portOut = *pport;
        ts = *pts;
    }

    u64 now = timeNowMS();
    return ts + _wellKnownValidTime < timeNowMS()
        ? EXPIRED
        : VALID;
}


// TODO periodic defragment auth to get rid of leftover map keys

bool MxStore::save()
{
    return false;
}
