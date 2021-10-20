#pragma once

#include "variant.h"
#include "treemem.h"
#include <vector>
#include <string>

class Fetcher
{
public:
    Fetcher();
    ~Fetcher();

    bool init(VarCRef config);

    Var fetchOne(TreeMem& mem, VarCRef spec) const;
    Var fetchAll(TreeMem& mem, VarCRef spec) const;

private:
    std::vector<std::string> _env;
};

