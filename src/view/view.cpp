#include "view.h"
#include "viewparser.h"


View::View(TreeMem& mem)
    : exe(mem)
{
}

View::~View()
{
}

bool View::compile(const char *s, VarCRef val)
{
    printf("%s\n", s);

    if (const char *code = val.asCString())
    {
        std::string err;
        if (size_t idx = view::parse(exe, code, err))
        {
            view::EntryPoint e { s, idx };
            ep.push_back(std::move(e));
            return true;
        }
        else
            printf("Key [%s] parse error:\n%s\n", s, err.c_str());
    }
    else
        printf("Key [%s] is not string value; skipped\n", s);
    return false;
}

bool View::load(VarCRef v)
{
    bool ok = true;
    if(VarCRef result = v.lookup("result"))
    {
        ok = compile("result", result) && ok;
    }
    else
    {
        printf("Key 'result' not present, abort\n");
        ok = false;
    }

    if(VarCRef lookup = v.lookup("lookup"))
    {
        if(lookup.type() != Var::TYPE_MAP)
            ok = false;

        const Var::Map *m = lookup.v->map();
        for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
        {
            ok = compile(v.mem->getS(it->first), VarCRef(v.mem, &it->second)) && ok;
        }

    }
    return ok;
}

bool View::initVM(view::VM& vm)
{
    return false;
}
