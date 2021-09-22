#include "debugfunc.h"

void varToString(std::ostringstream& os, VarCRef v)
{
    os << "Var<" << v.v->typestr() << ">(";
    switch (v.type())
    {
    case Var::TYPE_NULL:
        os << "null"; break;
    case Var::TYPE_BOOL:
        os << v.asBool(); break;
    case Var::TYPE_INT:
        os << *v.asInt(); break;
    case Var::TYPE_UINT:
        os << *v.asUint(); break;
    case Var::TYPE_FLOAT:
        os << *v.asFloat(); break;
    case Var::TYPE_STRING:
        os << v.asCString(); break;
    case Var::TYPE_PTR:
        os << v.v->asPtr(); break;
    case Var::TYPE_ARRAY:
    case Var::TYPE_MAP:
        os << '#' << v.size(); break;
    case Var::TYPE_RANGE:
    {
        const size_t n = v.size();
        const Var::Range *ra = v.asRange();
        for(size_t i = 0; i < n; ++i, ++ra)
        {
            if(i)
                os << ",";
            if(ra->first == ra->last+1)
                os << ra->first;
            else
                os << ra->first << ':' << ra->last;
        }
    }
    break;
    }
    os << ')';
}
