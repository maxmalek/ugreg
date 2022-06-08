#include "mxstore.h"
#include "util.h"
#include "json_in.h"
#include "json_out.h"
#include "mxtoken.h"
#include "rng.h"
#include "tomcrypt.h"
#include "scopetimer.h"

// Yes, the entries in authdata are stringly typed.
// But this way saving/restoring the entire structure is very simple if we ever need it
// (just dump as json, then load later)

#ifdef _DEBUG
#define DEBUG_SAVE // <-- define this to serialize auth state to disk every time. intended for actively debugging/developing
#endif

static const u64 second = 1000;
static const u64 minute = second * 60;
static const u64 hour = minute * 60;

MxStore::Config::Hash::Hash()
    : lazy(true)
{
}


MxStore::Config::Config()
{
    // default config
    hashcache.pepperTime = 1 * hour;
    hashcache.pepperLenMin = 24;
    hashcache.pepperLenMax = 40;
    wellknown.cacheTime = 1 * hour;
    wellknown.failTime = 10 * minute;
    register_.maxTime = 24 * hour;

    hashes["sha256"] = Hash();
}


MxStore::MxStore()
    : authdata(DataTree::SMALL), wellknown(DataTree::SMALL), hashcache(DataTree::DEFAULT)
    , hashPepperTS(0)
{
#ifdef DEBUG_SAVE
    load("debug.mxstore");
#endif
    authdata.root().makeMap();

    // TODO: enable hashcache entries from config
}

static bool readUint(u64& dst, VarCRef ref)
{
    if(!ref)
        return true;
    if(ref.type() != Var::TYPE_UINT)
        return false;
    dst = *ref.asUint();
    return true;
}

static bool readUintKey(u64& dst, VarCRef m, const char *key)
{
    if(!m)
        return true;
    return readUint(dst, m.lookup(key));
}


static bool readBool(bool& dst, VarCRef ref)
{
    if(!ref)
        return true;
    dst = ref.asBool();
    return true;
}

static bool readBoolKey(bool& dst, VarCRef m, const char *key)
{
    if(!m)
        return true;
    return readBool(dst, m.lookup(key));
}
static bool readTimeKey(u64& dst, VarCRef m, const char *key)
{
    if(!m)
        return true;
    VarCRef ref = m.lookup(key);
    if(!ref)
        return true;
    if(ref.type() != Var::TYPE_STRING)
        return false;
    PoolStr ps = ref.asString();
    return strToDurationMS_Safe(&dst, ps.s, ps.len);
}

bool MxStore::apply(VarCRef config)
{
    if(config.type() != Var::TYPE_MAP)
        return false;

    Config cfg;

    VarCRef xhashcache = config.lookup("hashcache");
    VarCRef xwellknown = config.lookup("wellknown");
    VarCRef xregister = config.lookup("register");
    VarCRef xhashes = config.lookup("hashes");

    bool ok =
           readTimeKey(cfg.hashcache.pepperTime, xhashcache, "pepperTime")
        && readTimeKey(cfg.wellknown.cacheTime, xwellknown, "cacheTime")
        && readTimeKey(cfg.wellknown.failTime, xwellknown, "failTime")
        && readTimeKey(cfg.wellknown.requestTimeout, xwellknown, "requestTimeout")
        && readUintKey(cfg.wellknown.requestMaxSize, xwellknown, "requestMaxSize")
        && readTimeKey(cfg.register_.maxTime, xregister, "maxTime");

    if(ok && xhashcache)
        if(VarCRef xpepperlen = xhashcache.lookup("pepperLen"))
        {
            if(xpepperlen.type() == Var::TYPE_UINT)
                cfg.hashcache.pepperLenMin = cfg.hashcache.pepperLenMax = *xpepperlen.asUint();
            else if(xpepperlen.type() == Var::TYPE_ARRAY)
            {
                ok = readUint(cfg.hashcache.pepperLenMin, xpepperlen.at(0))
                  && readUint(cfg.hashcache.pepperLenMax, xpepperlen.at(1));
            }
            else
                ok = false;
        }

    if(ok && xhashes)
    {
        const Var::Map *m = xhashes.v->map();
        if(m)
        {
            for(Var::Map::Iterator it = m->begin();  it != m->end(); ++it)
            {
                PoolStr hashname = xhashes.mem->getSL(it.key());
                if(strcmp(hashname.s, "none") && !hash_getdesc(hashname.s))
                {
                    printf("MxStore: Unknown hash [%s] in config, ignoring\n", hashname.s);
                    continue;
                }

                VarCRef t = VarCRef(xhashes.mem, &it.value());

                if(t.type() == Var::TYPE_MAP)
                {
                    Config::Hash h;
                    ok = ok && readBoolKey(h.lazy, t, "lazy");

                    cfg.hashes[hashname.s] = h;
                }
                else // just a bool or none entry? apply default or delete entry
                {
                    bool on = t.asBool();
                    if(on)
                        cfg.hashes[hashname.s] = Config::Hash();
                    else
                        cfg.hashes.erase(hashname.s);
                }
            }
        }
        else
            ok = false;
    }
    ok = ok && cfg.hashcache.pepperLenMin <= cfg.hashcache.pepperLenMax;
    if(!ok)
    {
        printf("MxStore::apply(): Failed to apply config\n");
        return false;
    }

    // config looks good, apply
    printf("MxStore: pepper len = %ju .. %ju\n", cfg.hashcache.pepperLenMin, cfg.hashcache.pepperLenMax);
    printf("MxStore: pepper time = %ju seconds\n", cfg.hashcache.pepperTime / 1000);
    printf("MxStore: wellknown cache time = %ju seconds\n", cfg.wellknown.cacheTime / 1000);
    printf("MxStore: wellknown fail time = %ju seconds\n", cfg.wellknown.failTime / 1000);
    printf("MxStore: wellknown request timeout = %ju ms\n", cfg.wellknown.requestTimeout);
    printf("MxStore: wellknown request maxsize = %ju bytes\n", cfg.wellknown.requestMaxSize);
    printf("MxStore: register max time = %ju seconds\n", cfg.register_.maxTime / 1000);
    for(Config::Hashes::const_iterator it = cfg.hashes.begin(); it != cfg.hashes.end(); ++it)
        printf("MxStore: Use hash [%s], lazy = %u\n", it->first.c_str(), it->second.lazy);

    this->config = cfg;
    return true;
}

void MxStore::defrag()
{
    // TODO
}

bool MxStore::register_(const char* token, size_t tokenLen, u64 expireInMS, const char *account)
{
    std::lock_guard lock(authdata.mutex);
    //---------------------------------------
    u64 expiryTime = timeNowMS() + expireInMS;
    PoolStr tok = { token, tokenLen };
    VarRef u = authdata.root()[tok];
    if(!u.isNull())
        return false;
    u["expiry"] = expiryTime;
    u["account"] = account;

#ifdef DEBUG_SAVE
    save_nolock("debug.mxstore");
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
    printf("Cache HS: [%s] -> [%s:%u]\n", host, hs, port);
    std::lock_guard lock(wellknown.mutex);
    //---------------------------------------
    VarRef u = wellknown.root()[host];
    u["homeserver"] = hs;
    u["port"] = u64(port);
    u["ts"] = timeNowMS();
}

void MxStore::storeFailForHost(const char* host)
{
    printf("Cache fail for host: [%s]\n", host);
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
            return *pFailTs + config.wellknown.failTime < timeNowMS()
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
    return ts + config.wellknown.cacheTime < timeNowMS()
        ? EXPIRED
        : VALID;
}

std::string MxStore::getHashPepper(bool allowUpdate)
{
    std::lock_guard lock(hashcache.mutex);
    //---------------------------------------

    if(allowUpdate)
    {
        u64 now = timeNowMS();
        if(hashPepperTS + config.hashcache.pepperTime < now)
            rotateHashPepper_nolock(now);
    }

    return hashPepper;
}

void MxStore::rotateHashPepper()
{
    u64 now = timeNowMS();
    std::lock_guard lock(hashcache.mutex);
    //---------------------------------------
    rotateHashPepper_nolock(now);
}

MxError MxStore::hashedBulkLookup(VarRef dst, VarCRef in, const char *algo, const char *pepper)
{
    Var::Map* m = dst.makeMap().v->map();
    const Var *a = in.v->array();
    const size_t n = in.size();
    assert(a);

    std::lock_guard lock(hashcache.mutex);
    //---------------------------------------

    VarRef cache = hashcache.root().lookup(algo);
    if(!cache)
        return M_INVALID_PARAM; // unsupported algo

    // This will also rotate the pepper if necessary
    std::string mypepper = this->getHashPepper(true);
    if(mypepper != pepper)
        return M_INVALID_PEPPER;

    // value is 'false' if marked as 'no cache available' aka 'cache was dropped'
    if(cache.type() == Var::TYPE_BOOL && !cache.asBool())
    {
        MxError err = _generateHashCache_nolock(cache, algo);
        if(err != M_OK)
            return err;
    }

    for(size_t i = 0; i < n; ++i)
    {
        PoolStr pshash = a[i].asString(*in.mem);

        // Client sent padded base64? Trim the padding.
        while(pshash.len && pshash.s[pshash.len-1] == '=')
            --pshash.len;

        if(!pshash.len)
            continue;

        if(VarRef v = cache.lookup(pshash.s, pshash.len))
        {
            PoolStr ps = v.asString();
            if(ps.s)
                dst[pshash].setStr(ps.s, ps.len);
        }
    }
    return M_OK;
}

bool MxStore::merge3pid(VarCRef root)
{
    std::lock_guard lock(threepid.mutex);
    //---------------------------------------
    return threepid.root().merge(root, MERGE_RECURSIVE);
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

void MxStore::rotateHashPepper_nolock(u64 now)
{
    int r = RandomNumberBetween((int)config.hashcache.pepperLenMin, (int)config.hashcache.pepperLenMax);
    hashPepper = GenerateHashPepper(r);
    hashPepperTS = now;
    printf("Hash pepper update, is now [%s]\n", hashPepper.c_str());
    _clearHashCache_nolock();

    // rehash for all hashes with lazy==false
    for(Config::Hashes::const_iterator it = config.hashes.begin(); it != config.hashes.end(); ++it)
    {
        const Config::Hash &h = it->second;
        if(!h.lazy)
            _generateHashCache_nolock(hashcache.root(), it->first.c_str());
    }
}

static const unsigned char s_space = ' ';

MxError MxStore::_generateHashCache_nolock(VarRef cache, const char* algo)
{
    // only "none" is fine, otherwise we need a valid descriptor
    const ltc_hash_descriptor * hd = NULL;
    if(strcmp(algo, "none"))
    {
        hd = hash_getdesc(algo);
        if(!hd)
            return M_INVALID_PARAM;
    }

    printf("Generating hash cache for [%s]...\n", algo);

    Var::Map *mdst = cache.makeMap().v->map();

    // TODO: lock threepid?

    const Var::Map *mmed = threepid.root().v->map();

    unsigned char *hashOut = (unsigned char*)(hd ? alloca(hd->hashsize) : NULL);
    const size_t hashBase64Len = hd ? base64size(hd->hashsize) : 0;
    char *hashBase64 = (char*)(hd ? alloca(hashBase64Len) : NULL);

    ScopeTimer timer;

    for(Var::Map::Iterator j = mmed->begin(); j != mmed->end(); ++j)
    {
        StrRef mediumref = j.key();
        PoolStr mediumps = threepid.getSL(mediumref);

        size_t done = 0;
        printf("... medium \"%s\"...", mediumps.s);

        const Var::Map *m = j.value().map();
        assert(m);

        // TODO: this could be done in parallel as long as we output to a local temp array and merge in the end

        if(hd)
        {
            for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
            {
                PoolStr kps = threepid.getSL(it.key()); // key: some 3pid
                PoolStr ups = it.value().asString(threepid); // value: mxid
                assert(kps.s && ups.s);
                hash_state h;
                hd->init(&h);
                hd->process(&h, (const unsigned char*)kps.s, kps.len);
                hd->process(&h, &s_space, 1);
                hd->process(&h, (const unsigned char*)mediumps.s, mediumps.len);
                hd->process(&h, &s_space, 1);
                hd->process(&h, (const unsigned char*)hashPepper.c_str(), hashPepper.length());
                hd->done(&h, hashOut);

                size_t enc = base64enc(hashBase64, hashBase64Len, hashOut, hd->hashsize, false);
                if(enc)
                {
                    mdst->putKey(*cache.mem, hashBase64, enc).setStr(*cache.mem, ups.s, ups.len);
                    ++done;
                }
            }

        }
        else // "none"
        {
            std::string tmp;
            for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
            {
                PoolStr kps = threepid.getSL(it.key()); // key: some 3pid
                PoolStr ups = it.value().asString(threepid); // value: mxid
                assert(kps.s && ups.s);
                tmp = kps.s;
                tmp += ' ';
                tmp += mediumps.s;
                // don't need the pepper here
                mdst->putKey(*cache.mem, tmp.c_str(), tmp.length()).setStr(*cache.mem, ups.s, ups.len);
                ++done;
            }
        }

        printf(" %zu entries done\n", done);
    }
    printf("... done generating cache, took %ju ms\n", timer.ms());
    return M_OK;
}

void MxStore::_clearHashCache_nolock()
{
    Var::Map *m = hashcache.root().v->map();
    if(m)
        for(Var::Map::MutIterator it = m->begin(); it != m->end(); ++it)
            it.value().setBool(hashcache, false);
    printf("Hash cache cleared\n");
}

std::string MxStore::GenerateHashPepper(size_t n)
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_01234567890!^$%&/()[]{}<>=?#+*~,.-_:;@|";
    std::string s;
    s.resize(n);
    mxGenerateToken(s.data(), n, alphabet, sizeof(alphabet) - 1, false);
    return s;
}
