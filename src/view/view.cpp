#include "view.h"
#include "viewparser.h"


View::View(TreeMem& mem)
    : exe(mem)
{
}

View::~View()
{
}

bool View::load(VarCRef v)
{
    if(VarCRef lookup = v.lookup("lookup"))
    {
        if(lookup.type() != Var::TYPE_MAP)
            return false;

        const Var::Map *m = lookup.v->map();
        for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
        {
            printf("lookup/%s\n", v.mem->getS(it->first));

            if(it->second.type() == Var::TYPE_STRING)
            {
                if(!view::parse(exe, it->second.asCString(*v.mem)))
                    printf("Failed to parse lookup/%s: %s\n",
                        v.mem->getS(it->first), it->second.asCString(*v.mem));
            }
            else
                printf("Key lookup/%s is not string value; skipped\n", v.mem->getS(it->first));
        }

    }
    return true;
}

bool View::initVM(view::VM& vm)
{
    return false;
}
