#include "mxstore.h"
#include "util.h"
#include "json_in.h"
#include "json_out.h"

// Yes, the entries in authdata are stringly typed.
// But this way saving/restoring the entire structure is very simple if we ever need it
// (just dump as json, then load later)

#ifdef _DEBUG
#define DEBUG_SAVE // <-- define this to serialize auth state to disk every time. intended for actively debugging/developing
#endif

static const u64 second = 1000;
static const u64 minute = second * 60;
static const u64 hour = minute * 60;

MxStore::MxStore()
    : authdata(DataTree::SMALL), wellknown(DataTree::SMALL)
    , _wellKnownValidTime(1 * hour)
    , _wellKnownFailTime(10 * minute)
{
#ifdef DEBUG_SAVE
    load("debug.mxstore");
#endif
    authdata.root().makeMap();
}

bool MxStore::register_(const char* token, size_t expireInSeconds, const char *account)
{
    std::lock_guard lock(authdata.mutex);
    //---------------------------------------
    u64 expiryTime = timeNowMS() + expireInSeconds * 1000;
    VarRef u = authdata.root()[token];
    if(!u.isNull())
        return false;
    u["expiry"] = expiryTime;
    u["account"] = account;

#ifdef DEBUG_SAVE
    save("debug.mxstore");
#endif

    return true;
}

MxError MxStore::authorize(const char* token) const
{
    std::lock_guard lock(authdata.mutex);
    //---------------------------------------
    VarCRef ref = authdata.root().lookup(token);
    if(!ref || ref.isNull())
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
    //---------------------------------------
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
    //---------------------------------------
    VarRef ref = authdata.root().lookup(token);
    ref.clear(); // map doesn't support deleting individual keys, so just set to TYPE_NULL for now
}

void MxStore::storeHomeserverForHost(const char* host, const char* hs, unsigned port)
{
    assert(hs && *hs && port);
    std::lock_guard lock(wellknown.mutex);
    //---------------------------------------
    VarRef u = wellknown.root()[host];
    u["homeserver"] = hs;
    u["port"] = u64(port);
    u["ts"] = timeNowMS();
}

void MxStore::storeFailForHost(const char* host)
{
    // FIXME: might want to use a CacheTable<> instead
    std::lock_guard lock(wellknown.mutex);
    //---------------------------------------
    wellknown.root()[host] = timeNowMS();
}

MxStore::LookupResult MxStore::getCachedHomeserverForHost(const char* host, std::string& hsOut, unsigned& portOut) const
{
    u64 ts;
    {
        std::lock_guard lock(wellknown.mutex);
        //---------------------------------------
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

bool MxStore::save(const char* fn) const
{
    std::lock_guard lock(authdata.mutex);
    //---------------------------------------
    return save_nolock(fn);
}

bool MxStore::load(const char* fn)
{
    std::lock_guard lock(authdata.mutex);
    //---------------------------------------
    return load_nolock(fn);
}


// TODO periodic defragment auth to get rid of leftover map keys

bool MxStore::save_nolock(const char *fn) const
{
    bool ok = false;
    std::string tmp = fn;
    tmp += ".new";
    if(FILE* f = fopen(tmp.c_str(), "wb"))
    {
        {
            char buf[12*1024];
            BufferedFILEWriteStream wr(f, buf, sizeof(buf));
            writeJson(wr, authdata.root(), true);
            wr.Flush();
            printf("MxStore::save: Wrote %zu bytes\n", wr.Tell());
        }
        fclose(f);
        remove(fn);
        ok = !rename(tmp.c_str(), fn);
    }
    return ok;
}

bool MxStore::load_nolock(const char *fn)
{
    bool ok = false;
    if(FILE* f = fopen(fn, "rb"))
    {
        char buf[12*1024];
        BufferedFILEReadStream fs(f, buf, sizeof(buf));
        ok = loadJsonDestructive(authdata.root(), fs);
        printf("MxStore::load: Read %zu bytes, success = %u\n", fs.Tell(), ok);
        fclose(f);
    }
    return ok;
}

