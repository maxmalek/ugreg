#pragma once

#include "variant.h"
#include "viewexec.h"

namespace view {

// A View is a collection of entrypoints into an associated Executable.
// Pass this to a VM ctor for a complete init. The View can be deleted later,
// a VM makes its own copy of the data.
// Avoid using this directly; it's a part of view::Mgr
class View
{
public:
    View(TreeMem& mem);
    View(View&& o) noexcept;
    View(const View&) = delete;
    ~View();

    bool load(VarCRef v); // load JSON structure describing the view

    Executable exe;
    std::vector<EntryPoint> ep;

private:
    bool compile(const char* s, VarCRef val);

    // The returned result is created using mem
    Var produceResult(TreeMem& mem, VarCRef root, VarCRef vars);
};

} // end namespace view
