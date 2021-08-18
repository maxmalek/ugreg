#include "variant.h"
#include "subproc.h"
#include "datatree.h"

#include <string>
#include <future>

// Returns 0 when no expiry was encountered in the entire subtree.
// If not 0, it's the minimal time of validity seen in the subtree, in ms.
u64 getTreeMinExpiryTime(VarCRef ref);

struct TreeMergeResult
{
    TreeMergeResult();
    bool ok;
    //u64 expiryTime;
};

// async call! returns immediately, but starts some background processing that will
// eventually succeed (or not)
std::future<TreeMergeResult> loadAndMergeJsonFromProcess(DataTree *dst, const AsyncLaunchConfig& cfg, const std::string& where, MergeFlags merge);

std::future<TreeMergeResult> loadAndMergeJsonFromFile(DataTree* dst, const std::string& file, const std::string& where, MergeFlags merge);
