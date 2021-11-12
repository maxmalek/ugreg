#pragma once

#include "variant.h"
#include "treemem.h"
#include "view.h"
#include <vector>
#include <string>

struct subprocess_s;

class Fetcher
{
public:
    static Fetcher *New(TreeMem& mem, VarCRef config);
    void destroy();

    bool fetchOne(VarRef dst, const char *suffix) const;
    bool fetchAll(VarRef dst, const char *suffix) const;

private:
    Fetcher(TreeMem& mem);
    ~Fetcher();
    bool init(VarCRef config);

    void _prepareEnv(VarCRef config);
    bool _doStartupCheck(VarCRef config) const;
    bool _fetch(VarRef dst, const view::View& vw, const char *path) const;
    bool _createProcess(subprocess_s *proc, VarCRef launch, int options) const;

    bool _useEnv;
    VarCRef _config; // references the original config. assumed not to change afterwards.
    size_t pathparts; // TODO: use
    u64 validity; // TODO: use
    std::vector<std::string> _env;
    view::View fetchsingle, fetchall;
    //view::View *postproc;
    // TODO: fail cache
};

