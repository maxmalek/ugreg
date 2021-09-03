#pragma once

#include "variant.h"
#include "viewexec.h"

class View
{
public:
    View(TreeMem& mem);
    ~View();

    bool load(VarCRef v);
    bool initVM(view::VM& vm);

    view::Executable exe;
    std::vector<view::EntryPoint> ep;

private:
    bool compile(const char* s, VarCRef val);
};

