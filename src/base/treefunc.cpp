#include "treefunc.h"
#include "treeiter.h"
#include "json_in.h"
#include <mutex>
#include "upgrade_mutex.h"
#include <utility>
#include <type_traits>
#include "util.h"

TreeMergeResult::TreeMergeResult()
    : ok(false)/*, expiryTime(0)*/
{
}


struct ExpiryFunctor : public ConstTreeIterFunctor
{
    ExpiryFunctor()
        : minexpiry(0)
    {}

    bool operator()(VarCRef v) const { return true; }
    void EndArray(VarCRef v) {}
    void EndObject(VarCRef v)
    {
        if(u64 exp = v.v->map_unsafe()->getExpiryTime())
            minexpiry = minexpiry ? std::min(minexpiry, exp) : exp;
    }
    void Key(const char* k, size_t len) {} // encountered a map key (op() will be called next)

    u64 minexpiry;
};

u64 getTreeMinExpiryTime(VarCRef ref)
{
    ExpiryFunctor f;
    treeIter_T(f, ref);
    return f.minexpiry;
}

static void finalizeAndMerge(TreeMergeResult& res, DataTree& dst, DataTree& t, const std::string& where, MergeFlags merge)
{
    //res.expiryTime = getTreeMinExpiryTime(t.root());
    { // BEGIN WRITE LOCK
        std::unique_lock lock(dst.mutex);
        logerror("Begin merge into [%s], flags = %u", where.c_str(), merge);
        if (VarRef sub = dst.subtree(where.c_str(), Var::SQ_CREATE))
            res.ok = sub.merge(t.root(), merge);
    } // END WRITE LOCK
}


static TreeMergeResult _loadAndMergeJsonFromProcess(DataTree *dst, AsyncLaunchConfig cfg, std::string where, MergeFlags merge)
{
    logdebug("Begin loading proc [%s] to %s", cfg.args[0].c_str(), where.c_str());
    DataTree *t = loadJsonFromProcessSync(std::move(cfg));

    TreeMergeResult res;

    if(t)
    {
        finalizeAndMerge(res, *dst, *t, where, merge);

        if(res.ok)
            log("Merged proc [%s] to %s", cfg.args[0].c_str(), where.c_str());
        else
            logerror("Failed to get subtree %s", where.c_str());

        logdebug("Cleaning up [%s] ...", cfg.args[0].c_str());
        delete t;
    }
    else
        logerror("Failed to load json from proc [%s] (bad json?)", cfg.args[0].c_str());

    logdebug("End loading proc [%s] to %s", cfg.args[0].c_str(), where.c_str());

    return res;
}

static TreeMergeResult _loadAndMergeJsonFromFile(DataTree* dst, std::string file, std::string where, MergeFlags merge)
{
    TreeMergeResult res;
    FILE* f = fopen(file.c_str(), "rb");
    if(!f)
    {
        logerror("Failed to open file [%s] to merge to %s", file.c_str(), where.c_str());
        return res;
    }
    logdebug("Begin loading file [%s] to %s", file.c_str(), where.c_str());

    {
        DataTree tree;
        char buf[12*1024];
        BufferedFILEReadStream fs(f, buf, sizeof(buf));
        bool loaded = loadJsonDestructive(tree.root(), fs);
        fclose(f);

        if(loaded)
        {
            logdebug("Finished loading file [%s]", file.c_str());

            finalizeAndMerge(res, *dst, tree, where, merge);

            if(res.ok)
                log("Merged file [%s] to %s", file.c_str(), where.c_str());
            else
                logerror("Failed to get subtree %s", where.c_str());
        }
        else
            logerror("Error loading file [%s] (bad json?)", file.c_str());

        logdebug("Cleaning up [%s] ...", file.c_str());
    }

    logdebug("End loading file [%s] to %s", file.c_str(), where.c_str());

    return res;
}

// std::async returns a future that, in its dtor, waits to be populated.
// for true fire-and-forget operation this kludge is required.
// via https://sodocumentation.net/cplusplus/topic/9840/futures-and-promises
template<typename F>
static auto async_deferred(F&& func) -> std::future<decltype(func())>
{
    auto task = std::packaged_task<decltype(func())()>(std::forward<F>(func));
    auto future = task.get_future();

    std::thread(std::move(task)).detach();

    return future;
}

std::future<TreeMergeResult> loadAndMergeJsonFromProcess(DataTree* dst, const AsyncLaunchConfig& cfg, const std::string& where, MergeFlags merge)
{
    //return std::async(std::launch::async, _loadAndMergeJsonFromProcess, dst, std::move(cfg), where, merge);
    return async_deferred([=]{
        return _loadAndMergeJsonFromProcess(dst, cfg, where, merge);
    });
}

std::future<TreeMergeResult> loadAndMergeJsonFromFile(DataTree* dst, const std::string& file, const std::string& where, MergeFlags merge)
{
    return async_deferred([=]{
        return _loadAndMergeJsonFromFile(dst, file, where, merge);
    });
}
