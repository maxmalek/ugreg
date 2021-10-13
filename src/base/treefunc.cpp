#include "treefunc.h"
#include "treeiter.h"
#include "json_in.h"
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <type_traits>

TreeMergeResult::TreeMergeResult()
    : ok(false)/*, expiryTime(0)*/
{
}


struct ExpiryFunctor : public ConstTreeIterFunctor
{
    ExpiryFunctor()
        : minexpiry(0)
    {}

    // Var was encountered. Return true to recurse (eventually End*() will be called).
    // Return false not to recurse (End*() will not be called)
    bool operator()(VarCRef v) const { return v.v->isContainer(); }

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
        std::unique_lock<std::shared_mutex> lock(dst.mutex);
        printf("Begin merge into [%s], flags = %u\n", where.c_str(), merge);
        if (VarRef sub = dst.subtree(where.c_str(), true))
            res.ok = sub.merge(t.root(), merge);
    } // END WRITE LOCK
}


static TreeMergeResult _loadAndMergeJsonFromProcess(DataTree *dst, AsyncLaunchConfig cfg, std::string where, MergeFlags merge)
{
    printf("Begin loading proc [%s] to %s\n", cfg.args[0].c_str(), where.c_str());
    DataTree *t = loadJsonFromProcessSync(std::move(cfg));

    TreeMergeResult res;

    if(t)
    {
        finalizeAndMerge(res, *dst, *t, where, merge);

        if(res.ok)
            printf("Merged proc [%s] to %s\n", cfg.args[0].c_str(), where.c_str());
        else
            printf("Failed to get subtree %s\n", where.c_str());

        printf("Cleaning up [%s] ...\n", cfg.args[0].c_str());
        delete t;
    }
    else
        printf("Failed to load json from proc [%s] (bad json?)\n", cfg.args[0].c_str());

    printf("End loading proc [%s] to %s\n", cfg.args[0].c_str(), where.c_str());

    return res;
}

static TreeMergeResult _loadAndMergeJsonFromFile(DataTree* dst, std::string file, std::string where, MergeFlags merge)
{
    TreeMergeResult res;
    FILE* f = fopen(file.c_str(), "rb");
    if(!f)
    {
        printf("Failed to open file [%s] to merge to %s\n", file.c_str(), where.c_str());
        return res;
    }
    printf("Begin loading file [%s] to %s\n", file.c_str(), where.c_str());

    {
        DataTree tree;
        char buf[12*1024];
        BufferedFILEReadStream fs(f, buf, sizeof(buf));
        bool loaded = loadJsonDestructive(tree.root(), fs);
        fclose(f);

        if(loaded)
        {
            printf("Finished loading file [%s]\n", file.c_str());

            finalizeAndMerge(res, *dst, tree, where, merge);

            if(res.ok)
                printf("Merged file [%s] to %s\n", file.c_str(), where.c_str());
            else
                printf("Failed to get subtree %s\n", where.c_str());
        }
        else
            printf("Error loading file [%s] (bad json?)\n", file.c_str());

        printf("Cleaning up [%s] ...\n", file.c_str());
    }

    printf("End loading file [%s] to %s\n", file.c_str(), where.c_str());

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
