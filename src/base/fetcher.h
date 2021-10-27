#pragma once

#include "variant.h"
#include "treemem.h"
#include <vector>
#include <string>

struct subprocess_s;

class Fetcher
{
public:
    Fetcher();
    ~Fetcher();

    bool init(VarCRef config);

    Var fetchOne(TreeMem& mem, VarCRef spec) const;
    Var fetchAll(TreeMem& mem, VarCRef spec) const;

private:
    void _prepareEnv(VarCRef config);
    bool _doStartupCheck(VarCRef config) const;
    bool _fetch(TreeMem& dst, VarCRef launch, const char *path) const;
    bool _createProcess(subprocess_s *proc, VarCRef launch, int options) const;

    bool _useEnv;
    size_t pathparts;
    std::vector<std::string> _env;
    // TODO: fail cache
};

