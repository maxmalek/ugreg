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
    }
    os << ')';
}