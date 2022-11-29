#include "mxstore.h"
#include "util.h"
#include "json_in.h"
#include "json_out.h"
#include "mxtoken.h"
#include "rng.h"
#include "tomcrypt.h"
#include "scopetimer.h"
#include <future>
#include "strmatch.h"

static const u64 second = 1000;
static const u64 minute = second * 60;
static const u64 hour = minute * 60;

MxStore::Config::Hash::Hash()
    : lazy(true)
{
}


MxStore::Config::Config()
    : minSearchLen(2)
{
    // default config
    hashcache.pepperTime = 1 * hour;
    hashcache.pepperLenMin = 24;
    hashcache.pepperLenMax = 40;
    wellknown.cacheTime = 1 * hour;
    wellknown.failTime = 10 * minute;
    register_.maxTime = 24 * hour;
}


MxStore::MxStore()
    : authdata(DataTree::SMALL), wellknown(DataTree::SMALL), hashcache(DataTree::DEFAULT)
    , hashPepperTS(0)
{
    authdata.root().makeMap();
    threepid.root().makeMap()["_data"].clear(); // { _data = None }

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
    VarCRef xmedia = config.lookup("media");
    VarCRef xdirectory = config.lookup("directory");

    bool ok =
           readTimeKey(cfg.hashcache.pepperTime, xhashcache, "pepperTime")
        && readTimeKey(cfg.wellknown.cacheTime, xwellknown, "cacheTime")
        && readTimeKey(cfg.wellknown.failTime, xwellknown, "failTime")
        && readTimeKey(cfg.wellknown.requestTimeout, xwellknown, "requestTimeout")
        && readUintKey(cfg.wellknown.requestMaxSize, xwellknown, "requestMaxSize")
        && readTimeKey(cfg.register_.maxTime, xregister, "maxTime")
        && readUintKey(cfg.minSearchLen, config, "minSearchLen");

    if(ok && xdirectory && xdirectory.type() == Var::TYPE_STRING)
    {
        cfg.directory = xdirectory.asCString();
        if(!cfg.directory.empty() && cfg.directory.back() != '/')
            cfg.directory += '/';
    }

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

    if(ok && xmedia)
        if(const Var::Map *m = xmedia.v->map())
            for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
                if(const char *v = it.value().asCString(*xmedia.mem))
                    cfg.media[xmedia.mem->getS(it.key())] = v;

    ok = ok && cfg.hashcache.pepperLenMin <= cfg.hashcache.pepperLenMax;
    if(!ok)
    {
        printf("MxStore::apply(): Failed to apply config\n");
        return false;
    }

    // config looks good, apply
    if(cfg.directory.empty())
        printf("MxStore: Not touching the disk. RAM only.\n");
    else
        printf("MxStore: directory = %s\n", cfg.directory.c_str());

    printf("MxStore: minSearchLen = %zu\n", cfg.minSearchLen);
    printf("MxStore: pepper len = %ju .. %ju\n", cfg.hashcache.pepperLenMin, cfg.hashcache.pepperLenMax);
    printf("MxStore: pepper time = %ju seconds\n", cfg.hashcache.pepperTime / 1000);
    printf("MxStore: wellknown cache time = %ju seconds\n", cfg.wellknown.cacheTime / 1000);
    printf("MxStore: wellknown fail time = %ju seconds\n", cfg.wellknown.failTime / 1000);
    printf("MxStore: wellknown request timeout = %ju ms\n", cfg.wellknown.requestTimeout);
    printf("MxStore: wellknown request maxsize = %ju bytes\n", cfg.wellknown.requestMaxSize);
    printf("MxStore: register max time = %ju seconds\n", cfg.register_.maxTime / 1000);

    for(Config::Media::iterator it = cfg.media.begin(); it != cfg.media.end(); ++it)
        printf("MxStore: Using field [%s] as medium [%s]\n", it->first.c_str(), it->second.c_str());

    std::unique_lock lock(hashcache.mutex);
    // --------------------------------------------------

    for(Config::Hashes::const_iterator it = cfg.hashes.begin(); it != cfg.hashes.end(); ++it)
    {
        printf("MxStore: Use hash [%s], lazy = %u\n", it->first.c_str(), it->second.lazy);
        VarRef cache = hashcache.root()[it->first.c_str()];
        if(cache.type() != Var::TYPE_MAP)
            cache = false; // create dummy entry to signify the cache has to be generated
    }

    this->config = cfg;
    return true;
}

void MxStore::defrag()
{
    // TODO
}

bool MxStore::register_(const char* token, size_t tokenLen, u64 expireInMS, const char *account)
{
    std::unique_lock lock(authdata.mutex);
    //---------------------------------------
    u64 expiryTime = timeNowMS() + expireInMS;
    PoolStr tok = { token, tokenLen };
    VarRef u = authdata.root()[tok];
    if(!u.isNull())
        return false;
    u["expiry"] = expiryTime;
    u["account"] = account;

    return true;
}

MxError MxStore::authorize(const char* token) const
{
    std::shared_lock lock(authdata.mutex);
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
    std::shared_lock lock(authdata.mutex);
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
    std::unique_lock lock(authdata.mutex);
    //---------------------------------------
    VarRef ref = authdata.root().lookup(token);
    ref.clear(); // map doesn't support deleting individual keys, so just set to TYPE_NULL for now
}

void MxStore::storeHomeserverForHost(const char* host, const char* hs, unsigned port)
{
    assert(hs && *hs && port);
    printf("Cache HS: [%s] -> [%s:%u]\n", host, hs, port);
    std::unique_lock lock(wellknown.mutex);
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
    std::unique_lock lock(wellknown.mutex);
    //---------------------------------------
    wellknown.root()[host] = timeNowMS();
}

MxStore::LookupResult MxStore::getCachedHomeserverForHost(const char* host, std::string& hsOut, unsigned& portOut) const
{
    u64 ts;
    {
        std::shared_lock lock(wellknown.mutex);
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
    std::unique_lock lock(hashcache.mutex); // might rotate // FIXME: use the upgrade_lock properly?
    //---------------------------------------
    return getHashPepper_nolock(allowUpdate);
}

std::string MxStore::getHashPepper_nolock(bool allowUpdate)
{
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
    std::unique_lock lock(hashcache.mutex);
    //---------------------------------------
    rotateHashPepper_nolock(now);
}

MxError MxStore::hashedBulkLookup(VarRef dst, VarCRef in, const char *algo, const char *pepper)
{
    const Var *a = in.v->array();
    const size_t n = in.size();
    assert(a);

    std::shared_lock lock(hashcache.mutex);
    //---------------------------------------

    VarRef cache = hashcache.root().lookup(algo);
    if(!cache)
        return M_INVALID_PARAM; // unsupported algo

    // This will also rotate the pepper if necessary
    std::string mypepper = this->getHashPepper_nolock(true);
    if(mypepper != pepper)
        return M_INVALID_PEPPER;

    // value is 'false' if marked as 'no cache available' aka 'cache was dropped'
    if(cache.type() == Var::TYPE_BOOL && !cache.asBool())
    {
        MxError err = _generateHashCache_nolock(cache, algo);
        if(err != M_OK)
            return err;
    }

    const bool isNoneAlgo = !strcmp(algo, "none");

    // ---- begin actual lookup ---
    ScopeTimer timer;

    // try exact matches first
    // this is also the only way to look up hashed 3pids
    for(size_t i = 0; i < n; ++i)
    {
        PoolStr pshash = a[i].asString(*in.mem);

        // "none" is plaintext, not base64
        if(!isNoneAlgo)
        {
            // Client sent padded base64? Trim the padding.
            while(pshash.len && pshash.s[pshash.len-1] == '=')
                --pshash.len;
        }

        if(pshash.len < config.minSearchLen)
            continue;

        // NB: If we get invalid base64 here, there will simply be no match.
        // so we don't even need to decode what the client sent us
        if(VarRef v = cache.lookup(pshash.s, pshash.len))
        {
            PoolStr ps = v.asString();
            if(ps.s)
                dst[pshash].setStr(ps.s, ps.len);
        }
    }

    // and if not hashed, try to find identifiers that somewhat match
    if(isNoneAlgo)
        unhashedFuzzyLookup_nolock(dst, in);


    printf("MxStore: Lookup (%u in, %u out) took %ju ms\n",
        (unsigned)n, (unsigned)dst.size(), timer.ms());

    return M_OK;
}

// unhashed bulk lookup if "none" algo
MxError MxStore::unhashedFuzzyLookup_nolock(VarRef dst, VarCRef in)
{
    VarCRef cache = hashcache.root().lookup("none");
    const Var::Map *m = cache ? cache.v->map() : NULL;
    if(!m)
        return M_NOT_FOUND;

    // cache input strings so we don't have to go through the string pool every single time

    struct FindEntry
    {
        PoolStr addr, medium;
    };

    std::vector<FindEntry> find;
    {
        const Var *a = in.v->array();
        const size_t n = in.size();
        assert(a);

        find.reserve(n);
        for(size_t i = 0; i < n; ++i)
        {
            // this is probably in the format "3pid medium" -- split it up
            PoolStr addr = a[i].asString(*in.mem);
            PoolStr med = {};
            if(const char *spc = strchr(addr.s, ' '))
            {
                med.s = spc + 1;
                med.len = strlen(med.s);
                addr.len = spc - addr.s;
            }
            if(addr.len < config.minSearchLen)
                continue;
            printf("Plaintext lookup [%u]: %s (%s)\n", (unsigned)find.size(), addr.s, med.s);
            FindEntry f { addr, med };
            find.push_back(f);
        }
    }
    const size_t N = find.size();
    if(!N)
        return M_OK;

    for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
    {
        const PoolStr k = cache.mem->getSL(it.key());
        assert(k.s);
        size_t klen = k.len; // actually used length of k

        // in the cache, key is always "3pid medium" so we want to stop after the first space
        const char *medium = NULL;
        if(const char *spc = strchr(k.s, ' '))
        {
            medium = spc + 1;
            klen = spc - k.s;
        }

        if(!klen)
            continue;

        for(size_t i = 0; i < N; ++i)
        {
            if(klen < find[i].addr.len)
                continue;

            if(medium && find[i].medium.len && !strcmp(medium, find[i].medium.s))
                continue;

            // TODO: might want to use actual fuzzy search? can we order by relevance?
            const char *match = strstr(k.s, find[i].addr.s);
            if(!match || match >= k.s + klen)
                continue;

            // TODO: do we want to also search values (ie. mxids), or does the matrix server already do that?

            PoolStr ps = it.value().asString(*cache.mem);
            dst[k].setStr(ps.s, ps.len); // here we use the original k so that both 3pid and medium are part of the key
        }
    }

    // TODO: call uncached external 3pid providers?
    // Would be better to keep a cache for their results too
    // and only do an external call if uncached or too old

    return M_OK;
}

MxStore::SearchResults MxStore::formatMatches(const MxSearchConfig& scfg, const MxSearch::Match* matches, size_t n) const
{
    ScopeTimer timer;
    std::vector<SearchResult> res;
    res.reserve(n);

    {
        std::shared_lock lock(threepid.mutex);
        //---------------------------------------

        const VarCRef data = threepid.root().lookup("_data");
        const StrRef displaynameRef = data.mem->lookup(scfg.displaynameField.c_str(), scfg.displaynameField.length());
        const Var::Map * const m = data.v->map();

        for(size_t i = 0; i < n; ++i)
        {
            const StrRef key = matches[i].key;
            if(const Var *user = m->get(key))
                if(const Var::Map *um = user->map())
                {
                    PoolStr mxid = data.mem->getSL(key);
                    assert(mxid.s); // we just got the map key. this must exist.

                    SearchResult sr;
                    sr.str.assign(mxid.s, mxid.len);

                    if(const Var *xdn = um->get(displaynameRef))
                        if(const char *dn = xdn->asCString(threepid))
                            sr.displayname = dn;

                    res.push_back(std::move(sr));
                }
        }
    }

    printf("MxStore::formatMatches(): %zu/%zu results in %u ms\n",
        res.size(), n, unsigned(timer.ms()));

    return res;
}

void MxStore::merge3pid(VarCRef root)
{
    std::unique_lock lock(threepid.mutex);
    //---------------------------------------
    return merge3pid_nolock(root);
}

void MxStore::merge3pid_nolock(VarCRef root)
{
    VarRef mergedst = threepid.root()["_data"].makeMap();
    if(VarCRef src = root.lookup("data"))
        mergedst.merge(src, MERGE_RECURSIVE);
    else
        printf("MxStore: WARNING: merge3pid got map without 'data' key, using stale values");

    for(Config::Media::iterator it = config.media.begin(); it != config.media.end(); ++it)
    {
        const char *nameOfField = it->first.c_str();
        const char *nameOf3pid = it->second.c_str();
        VarRef dst = threepid.root()[nameOf3pid].makeMap();
        _Rebuild3pidMap(dst, mergedst, nameOfField);
    }
}

DataTree::LockedRef MxStore::get3pidRoot()
{
    return threepid.lockedRef();
}

DataTree::LockedCRef MxStore::get3pidCRoot() const
{
    return threepid.lockedCRef();
}

static bool _lockAndSave(const DataTree *tree, std::string fn)
{
    std::shared_lock lock(tree->mutex);
    return serialize::save(fn.c_str(), tree->root(), serialize::ZSTD, serialize::BJ);
}

static bool _lockAndLoad(DataTree *tree, std::string fn)
{
    std::unique_lock lock(tree->mutex);
    return serialize::load(tree->root(), fn.c_str(), serialize::ZSTD, serialize::BJ);
}

bool MxStore::save() const
{
    if(config.directory.empty())
        return false;

    printf("MxStore::save() starting...\n");

    ScopeTimer timer;
    bool ok = false;
    {
        auto authF     = std::async(_lockAndSave, &authdata, config.directory + "auth.mxs");
        auto threepidF = std::async(_lockAndSave, &threepid, config.directory + "threepid.mxs");
        ok = authF.get() && threepidF.get();
    }
    printf("MxStore::save() done in %u ms, success = %d\n", (unsigned)timer.ms(), ok);
    return ok;
}

bool MxStore::load()
{
    if(config.directory.empty())
        return false;

    ScopeTimer timer;
    bool ok = false;
    {
        auto authF     = std::async(_lockAndLoad, &authdata, config.directory + "auth.mxs");
        auto threepidF = std::async(_lockAndLoad, &threepid, config.directory + "threepid.mxs");
        ok = authF.get() && threepidF.get();
    }
    printf("MxStore::load() done in %u ms, success = %d\n", (unsigned)timer.ms(), ok);
    return ok;
}


// TODO periodic defragment auth to get rid of leftover map keys


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

void MxStore::markForRehash_nolock()
{
    hashPepperTS = 0;
}

/*static*/ std::string MxStore::GenerateHashPepper(size_t n)
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890";
    std::string s;
    s.resize(n);
    mxGenerateToken(s.data(), n, alphabet, sizeof(alphabet) - 1, false);
    return s;
}

void MxStore::_Rebuild3pidMap(VarRef dst, VarCRef src, const char* fromkey)
{
    assert(dst.mem == src.mem); // this simplifies things a lot and makes it really fast, too
    const Var::Map* m = src.v->map();
    Var::Map* dm = dst.v->map();

    assert(m && dm);

    StrRef keyref = src.mem->lookup(fromkey, strlen(fromkey));
    if (!keyref || !m || !dm)
        return; // string isn't present -> can't possibly have this as key

    ScopeTimer timer;
    size_t n = 0;

    for (Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
    {
        StrRef mxidRef = it.key();
        const Var* val = it.value().lookup(keyref); // val = d[fromkey]
        if (val && val->type() == Var::TYPE_STRING)
        {
            dm->getOrCreate(*dst.mem, mxidRef).setStrRef(*dst.mem, val->asStrRef()); // dst[mxid] = val
            ++n;
        }
    }

    printf("MxStore: Rebuilt 3pid from [%s], %zu entries in %llu ms\n", fromkey, n, timer.ms());
}
