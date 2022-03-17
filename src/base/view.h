#pragma once

#include "variant.h"
#include "viewexec.h"

namespace view {

// A View is a collection of named entrypoints into an associated Executable.
// Pass this to a VM ctor for a complete init. The View can be deleted later,
// a VM makes its own copy of the data (so that it can be run in a worker thread easily).
// -> Managed by view::Mgr but also for standalone use
class View
{
public:
    View(TreeMem& mem);
    View(View&& o) noexcept = delete;
    View(const View&) = delete;
    ~View();

    // Load JSON structure describing a view. All strings that are not keys are parsed and replaced with VM calls.
    // extended=false: load object as template. Filled-in object will be returned in produceResult().
    // extended=true: If it's a map, load "result" key as result, and everything else as variables. Otherwise load normally.
    bool load(VarCRef v, bool extended);
    bool loaded() const { return !exe.cmds.empty(); }

    // Runs a temporary VM. The returned result is created using dst
    Var produceResult(TreeMem& dst, VarCRef root, VarCRef vars) const;

    Executable exe;
    std::vector<EntryPoint> ep;

private:
    Var resultTemplate;
    size_t compile(const char* s, VarCRef val); // returns 0 on error

    bool _loadTemplate(VarCRef in);
};

} // end namespace view
