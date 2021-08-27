#include "viewexec.h"
#include <assert.h>
#include <utility>
#include <sstream>
#include "treemem.h"
#include "util.h"
#include "safe_numerics.h"
#include "debugfunc.h"

namespace view {

void StackFrame::clear(TreeMem& mem)
{
    const size_t n = store.size();
    for(size_t i = 0; i < n; ++i)
        store[i].clear(mem);
    store.clear();
    refs.clear();
}

// because pushing to frame.store may realloc the vector, either reserve frame.store
// with the correct size beforehand and then use addAbs(),
// or use addRel() to add stuff and call makeAbs() when done
void addRel(TreeMem& mem, StackFrame& frame, Var&& v)
{
    frame.refs.push_back(VarCRef(mem, (const Var*)(uintptr_t)(frame.store.size())));
    frame.store.push_back(std::move(v));
}
void addAbs(TreeMem& mem, StackFrame& frame, Var&& v)
{
    frame.store.push_back(std::move(v));
    frame.refs.push_back(VarCRef(mem, &frame.store.back()));
}
void makeAbs(StackFrame& frame)
{
    const Var *base = frame.store.data();
    const size_t n = frame.refs.size();
    for(size_t i = 0; i < n; ++i)
    {
        frame.refs[i].v = base + (uintptr_t)(frame.refs[i].v);
    }
}

// TODO: move transforms to own file
static void transformToInt(TreeMem& mem, StackFrame& newframe, const StackFrame& oldframe)
{
    const size_t n = oldframe.store.size();
    newframe.store.reserve(n);

    for(size_t i = 0; i < n; ++i)
    {
        s64 val;
        const VarCRef& src = oldframe.refs[i];
        switch(src.type())
        {
            case Var::TYPE_INT:
                val = *src.asInt();
                break;

            case Var::TYPE_UINT:
            {
                u64 tmp = *src.asUint();
                if(!isValidNumericCast<s64>(tmp))
                    continue;
                val = tmp;
            }
            break;

            case Var::TYPE_STRING:
            {
                const PoolStr ps = src.asString();
                if(!strtoi64NN(&val, ps.s, ps.len).ok())
                    continue;
            }
            break;

            default:
                continue;
        }
        addAbs(mem, newframe, std::move(Var(val)));
    }
}


VM::VM(const Executable& ex, VarCRef constants)
    : cmds(ex.cmds)
{
    // the constants are likely allocated in a different (shared!) memory space. copy everything so we have a private working copy.
    // this is especially important to make sure we can do fast string comparisons.
    if(constants)
        vars = std::move(constants.v->clone(*this, *constants.mem));

    // vars must be a map
    if(vars.type() != Var::TYPE_MAP)
        vars.makeMap(*this);

    // literals are only ever referred to by index, copy those as well
    const size_t N = ex.literals.size();
    Var *a = literals.makeArray(*this, N);
    for(size_t i = 0; i < N; ++i)
        a[i] = std::move(ex.literals[i].clone(*this, ex.mem));
}

VM::~VM()
{
    vars.clear(*this);
    literals.clear(*this);
}

// replace all maps on top with a subkey of each
void VM::cmd_GetKey(unsigned param)
{
    PoolStr ps = literals[param].asString(*this);

    StackFrame& top = _topframe();
    const size_t N = top.refs.size();
    VarCRef * const ain = top.refs.data();
    VarCRef *aout = ain;

    // TODO: make variant for json ptr
    assert(ps.s[0] != '/');

    for (size_t i = 0; i < N; ++i)
        if(const Var *sub = ain[i].lookup(ps.s)) // NULL if not map
        {
            aout->v = sub; // we're just going deeper, the mem stays the same
            ++aout;
        }
    top.refs.resize(aout - ain);
}

// keep elements in top only when a subkey has operator relation to a literal
void VM::cmd_CheckKey(unsigned param, unsigned lit)
{
    unsigned invert = param & 1;
    unsigned op = (param >> 1) & 7;
    unsigned key = param >> 4;

    StackFrame& top = _topframe();
    size_t wpos = 0;
    const size_t N = top.refs.size();
    VarCRef checklit(*this, &literals[lit]);
    Var::CompareMode cmp = Var::CompareMode(op);

    VarCRef* const ain = top.refs.data();
    VarCRef* aout = ain;

    for(size_t i = 0; i < N; ++i)
    {
        Var::CompareResult res = ain[i].compare(cmp, checklit);
        // Can't be compared, just skip it
        if (res == Var::CMP_RES_NA)
            continue;

        unsigned success = (res & 1) ^ invert;
        if (success)
            *aout++ = ain[i];
    }
    top.refs.resize(aout - ain);
}

void VM::push(VarCRef v)
{
    StackFrame frm;
    frm.refs.push_back(v);
    stack.push_back(std::move(frm));
}

bool VM::exec(size_t ip)
{
    if(stack.empty())
        return false;

    for(;;)
    {
        const Cmd& c = cmds[ip++];

        switch(c.type)
        {
            case CM_GETKEY:
                cmd_GetKey(c.param);
                break;
            case CM_CHECKKEY:
                cmd_CheckKey(c.param, c.param2);
                break;
            case CM_LITERAL:
                push(VarCRef(*this, &literals[c.param]));
                break;
            case CM_DUP:
            {
                StackFrame add;
                add.refs = stack[stack.size() - c.param - 1].refs; // just copy the refs, but not the storage
                stack.push_back(std::move(add));
            }
            break;

            case CM_DONE:
                return true;

            case CM_GETVAR: // TODO WRITE ME
            case CM_TRANSFORM:
            case CM_COMPARE:
                assert(false);
        }
    }
}

void VM::reset()
{
    while(stack.size())
        _popframe().clear(*this);
}

const VarRefs& VM::results() const
{
    return stack.back().refs;
}

StackFrame& VM::_topframe()
{
    return stack.back();
}

StackFrame VM::_popframe()
{
    StackFrame top = std::move(stack.back());
    stack.pop_back();
    return top;
}

unsigned GetTransformID(const char* s)
{
    return 0;
}

Executable::Executable(TreeMem& mem)
    : mem(mem)
{
}

Executable::~Executable()
{
    for(size_t i = 0; i < literals.size(); ++i)
        literals[i].clear(mem);
}

// ----------------------------------------------

static const char *s_opcodeNames[] =
{
    "GETKEY",
    "GETVAR",
    "TRANSFORM",
    "COMPARE",
    "LITERAL",
    "DUP",
    "CHECKKEY",
    "DONE"
};
static_assert(Countof(s_opcodeNames) == CM_DONE+1, "opcode enum vs name table mismatch");

static const char *s_operatorNames[] =
{
    "EQ",
    "LT",
    "GT",
    "CONTAINS",
    "STARTSWITH",
    "ENDSWITH",
};
//static_assert(Countof(s_operatorNames) == _OP_ARRAYSIZE, "operator enum vs name table mismatch");


static void oprToStr(std::ostringstream& os, unsigned opparam)
{
    unsigned invert = opparam & 1;
    unsigned op = opparam >> 1;
    if (invert)
        os << " NOT";
    os << ' ' << s_operatorNames[op];
}


size_t Executable::disasm(std::vector<std::string>& out) const
{
    const size_t n = cmds.size();
    for(size_t i = 0; i < n; ++i)
    {
        const Cmd& c = cmds[i];
        std::ostringstream os;
        os << s_opcodeNames[c.type];

        switch(cmds[i].type)
        {
            case CM_DUP:
            case CM_DONE:
                break; // do nothing
            case CM_GETVAR:
                os << ' ' << literals[c.param].asCString(mem);
                break;
            case CM_GETKEY:
            case CM_LITERAL:
                os << ' ';
                varToString(os, VarCRef(mem, &literals[c.param]));
                break;
            case CM_TRANSFORM:
                os << " (func #" << c.param << ")";
                break;
            case CM_COMPARE:
                os << ' ';
                oprToStr(os, c.param);
                break;

            case CM_CHECKKEY:
            {
                unsigned key = c.param >> 4;
                os << " [ " << literals[key].asCString(mem);
                oprToStr(os, c.param & 0xf);
                os << ' ';
                varToString(os, VarCRef(mem, &literals[c.param2]));
                os << " ]";
            }
            break;

            default:
                assert(false);

        }

        out.push_back(os.str());
    }
    return n;
}

} // end namespace view
