#include "view.h"
#include "viewparser.h"

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

Var View::produceResult(TreeMem& mem, VarCRef root, VarCRef vars)
{
    return Var();
}

bool View::load(VarCRef v)
{
    if (v.type() != Var::TYPE_MAP)
        return false;

    bool ok = true;
    const Var::Map *m = v.v->map();
    for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
    {
        ok = compile(v.mem->getS(it.key()), VarCRef(v.mem, &it.value())) && ok;
    }

    // TODO: check up-front that all referenced variables are there
    // also: add extra key: "external": ["a", ...] to make
    // variables that must be passed into the query (?a=...) explicit
    // so we can bail early when the client didn't supply one

    return ok;
}

} // end namespace view
