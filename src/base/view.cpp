#include "view.h"
#include "viewparser.h"
#include "treeiter.h"
#include <sstream>

namespace view {

View::View(TreeMem& mem)
    : exe(mem)
{
}

View::View(View&& o) noexcept
    : exe(std::move(o.exe))
    , ep(std::move(o.ep))
{
}

View::~View()
{
    resultTemplate.clear(*exe.mem);
}

size_t View::compile(const char *s, VarCRef val)
{
    printf("%s\n", s);

    if (const char *code = val.asCString())
    {
        std::string err;
        if (size_t idx = view::parse(exe, code, err))
        {
            view::EntryPoint e { s, idx };
            ep.push_back(std::move(e));
            return idx;
        }
        else
            printf("Key [%s] parse error:\n%s\n", s, err.c_str());
    }
    else
        printf("Key [%s] is not string value; skipped\n", s);
    return 0;
}

struct ViewProducerVisitor : public MutTreeIterFunctor
{
    VM& vm;
    const VarCRef base;

    ViewProducerVisitor(VM& vm, VarCRef base) : vm(vm), base(base) {}

    // Var was encountered. Return true to recurse (eventually End*() will be called).
    // Return false not to recurse (End*() will not be called)
    bool operator()(VarRef v)
    {
        assert(v.mem == &vm.mem);

        if(v.v->isContainer())
            return true; // recurse

        if(v.type() == Var::TYPE_PTR)
        {
            // TYPE_PTR is used to store an entry point, and is not actually a ptr here
            uintptr_t start = (uintptr_t)v.asPtr();
            v.v->clear(vm.mem);
            vm.run(base, start);
            *v.v = std::move(ExportResult(vm));
        }
        return false; // nothing to recurse
    }

    static Var ExportResult(const VM& vm)
    {
        Var ret;
        const view::VarRefs& out = vm.results();
        switch (out.size())
        {
            case 0:
                break; // just keep it null
            case 1:
                ret = out[0].ref.clone(vm.mem);
                break;
            default: // user didn't pay attention and multiple things were returned
                // -> make it an array with undefined ordering and hope for the best
            {
                size_t n = out.size();
                Var* a = ret.makeArray(vm.mem, n);
                for (size_t i = 0; i < n; ++i)
                    a[i] = out[i].ref.clone(vm.mem);
            }
        }
        return ret;
    }

    void EndArray(VarRef v) {}       // finished iterating over array
    void EndObject(VarRef v) {}      // finished iterating over map
    void Key(const char* k, size_t len) {} // encountered a map key (op() will be called next)
};


Var View::produceResult(TreeMem& dst, VarCRef root, VarCRef vars) const
{
    VM vm(dst);
    vm.init(exe, ep.data(), ep.size());

    if(vars)
    {
        const Var::Map *m = vars.v->map();
        if(!m)
        {
            printf("View::produceResult: Passed vars is not map, aborting\n");
            return Var();
        }

        for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
        {
            PoolStr name = vars.mem->getSL(it.key());
            VarRef v = vm.makeVar(name.s, name.len);
            *v.v = it.value().clone(*v.mem, *vars.mem);
        }
    }

    Var ret = resultTemplate.clone(dst, *exe.mem);

    // Exchange all entries of TYPE_PTR with results of VM calls
    ViewProducerVisitor vis(vm, root);
    treeIter_T(vis, VarRef(vm.mem, &ret));

    return ret;
}

// Iterate the tree, compile any encountered string values and replace them with VM entry points
// Any code encountered is recorded into exe
struct ViewTemplateCompilerVisitor : public MutTreeIterFunctor
{
    ViewTemplateCompilerVisitor(Executable& exe) : exe(exe), fail(false) {}

    Executable& exe;
    std::ostringstream errors;
    bool fail;

    // Var was encountered. Return true to recurse (eventually End*() will be called).
    // Return false not to recurse (End*() will not be called)
    bool operator()(VarRef v)
    {
        if(v.v->isContainer())
            return true; // recurse

        assert(v.type() != Var::TYPE_PTR);

        if(const char *code = v.asCString())
        {
            std::string err;
            if (size_t idx = view::parse(exe, code, err))
                v = (void*)(uintptr_t)idx;
            else
            {
                errors << err << "\n";
                fail = true;
            }
        }

        return false; // nothing to recurse
    }

    void EndArray(VarRef v) {}       // finished iterating over array
    void EndObject(VarRef v) {}      // finished iterating over map
    void Key(const char* k, size_t len) {} // encountered a map key (op() will be called next)
};

// Convert a result template into a working copy,
// ie. replace all string values with Var::TYPE_PTR.
// Those don't store an actual pointer; it's just an entry point casted to one
// because that type is otherwise unused in the data.
bool View::_loadTemplate(VarCRef in)
{
    resultTemplate.clear(*exe.mem);
    resultTemplate = std::move(in.clone(*exe.mem));

    ViewTemplateCompilerVisitor vis(this->exe);
    treeIter_T(vis, VarRef(exe.mem, &resultTemplate));

    return !vis.fail;
}

bool View::load(VarCRef v)
{
    VarCRef result;

    switch(v.type())
    {
        case Var::TYPE_MAP:
        {
            const Var::Map *m = v.v->map();
            VarCRef result;

            for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
            {
                size_t idx = 0;
                const char *key = v.mem->getS(it.key());
                // A key named "result" is the final product, everything else
                // are temporary variables
                if (!strcmp(key, "result"))
                    result = VarCRef(v.mem, &it.value());
                else
                {
                    idx = compile(key, VarCRef(v.mem, &it.value()));
                    if (!idx)
                    {
                        printf("Failed to compile key '%s'\n", key);
                        return false;
                    }
                }


            }
        }
        break;

        default:
            result = v;
            break;
    }

    if (result)
        _loadTemplate(result);
    else
    {
        printf("Not sure what the result of the view is supposed to be. Either make the result a map with a 'result' key if you need variables, or any other value to use that.\n");
        return false;
    }

    // TODO: check up-front that all referenced variables are there
    // also: add extra key: "external": ["a", ...] to make
    // variables that must be passed into the query (?a=...) explicit
    // so we can bail early when the client didn't supply one

    return true;
}

} // end namespace view
