#pragma once

#include "variant.h"
#include "treemem.h"
#include "view.h"
#include <vector>
#include <string>
#include <mutex>

struct subprocess_s;

class Fetcher : public TreeMem
{
public:
    static Fetcher *New(VarCRef config);
    void destroy();

    // The returned Var uses the fetcher's memory
    Var fetchOne(const char *suffix, size_t len);
    Var fetchAll();

    std::mutex mutex; // externally locked

private:
    Fetcher();
    ~Fetcher();
    bool init(VarCRef config);

    void _prepareEnv(VarCRef config);
    bool _prepareView(view::View& vw, VarCRef config, const char *key);
    bool _doStartupCheck(VarCRef config) const;
    Var _fetch(const view::View& vw, const char *path, size_t len);
    bool _createProcess(subprocess_s *proc, VarCRef launch, int options) const;

    // does not do post-single processing
    Var _fetchAllNoPost();
    Var _postproc(const view::View& vw, Var&& var);

    bool _useEnv;
    VarCRef _config; // references the original config. assumed not to change afterwards.
    u64 validity; // TODO: use
    std::vector<std::string> _env;
    view::View fetchsingle, fetchall, postall, postsingle;
    Var alldata; // used when fetchOne() is called but only fetchAll() is available

    //view::View *postproc;
    // TODO: fail cache
};

