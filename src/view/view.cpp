#include "view.h"
#include "viewparser.h"

namespace view {

View::View(TreeMem& mem)
    : exe(mem)
    , start(0)
{
}

View::View(View&& o) noexcept
    : exe(std::move(o.exe))
    , ep(std::move(o.ep))
    , start(0)
{
}

View::~View()
{
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

Var View::produceResult(TreeMem& mem, VarCRef root, VarCRef vars)
{
    VM vm(mem);
    vm.init(exe, ep.data(), ep.size());

    if(vars)
    {
        const Var::Map *m = vars.v->map();
        if(!m);
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

    for(size_t i = 0; i < starts.size(); ++i)
    {
        vm.run(root, starts[i]);

        Var ret;
        const view::VarRefs& out = vm.results();
        switch(out.size())
        {
            case 0:
                break; // just keep it null
            case 1:
                ret = out[0].ref.clone(mem);
                break;
            default: // user didn't pay attention and multiple things were returned
                // -> make it an array with undefined ordering and hope for the best
            {
                size_t n = out.size();
                Var *a = ret.makeArray(mem, n);
                for(size_t i = 0; i < n; ++i)
                    a[i] = out[i].ref.clone(mem);
            }
        }
    }

    return ret;
}

bool View::load(VarCRef v)
{
    bool ok = true;

    switch(v.type())
    {
        case Var::TYPE_MAP:
        {
            const Var::Map *m = v.v->map();

            for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
            {
                const char *key = v.mem->getS(it.key());
                size_t idx = compile(key, VarCRef(v.mem, &it.value()));
                if (!strcmp(key, "result"))
                    start = idx;
                ok = ok && idx;
            }
        }
        break;

        case Var::TYPE_ARRAY:
        {

        }
        break;

        case Var::TYPE_STRING:
            start = compile("result", v);
            break;

        default:
            printf("View dev is not map or string, this is invalid\n");
            return false;
    }

    if(!start)
    {
        printf("View def is a map but 'result' key does not exist\n");
        return false;
    }


    // TODO: check up-front that all referenced variables are there
    // also: add extra key: "external": ["a", ...] to make
    // variables that must be passed into the query (?a=...) explicit
    // so we can bail early when the client didn't supply one

    return ok;
}

} // end namespace view
