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

    bool load(VarCRef v); // load JSON structure describing the view, or a single string if no variables/temporaries are needed, or an array of strings

    Executable exe;
    std::vector<EntryPoint> ep;

private:
    Var resultTemplate;
    size_t compile(const char* s, VarCRef val); // returns 0 on error

    // The returned result is created using dst
    Var produceResult(TreeMem& dat, VarCRef root, VarCRef vars);

    Var _loadTemplate(VarCRef in);
};

} // end namespace view
