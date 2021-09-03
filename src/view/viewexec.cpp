#include "viewexec.h"
#include <assert.h>
#include <utility>
#include <sstream>
#include "treemem.h"
#include "util.h"
#include "safe_numerics.h"
#include "debugfunc.h"
#include "mem.h"

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

// TODO: move transforms to own file?

// unpack arrays, skip anything else
static void transformUnpack(TreeMem& mem, StackFrame& newframe, StackFrame& oldframe)
{
    newframe.store = std::move(oldframe.store);

    const size_t n = oldframe.store.size();
    size_t nn = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const VarCRef& src = oldframe.refs[i];
        if(src.type() == Var::TYPE_ARRAY)
            nn += src.size();
    }

    newframe.refs.reserve(nn);

    for (size_t i = 0; i < n; ++i)
    {
        const VarCRef& src = oldframe.refs[i];
        if (src.type() == Var::TYPE_ARRAY)
            for(size_t k = 0; k < nn; ++k)
                newframe.refs.push_back(src.at(k));
    }
}

static void transformToInt(TreeMem& mem, StackFrame& newframe, StackFrame& oldframe)
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

struct TransformEntry
{
    TransformFunc func;
    const char* name;
};

static const TransformEntry s_transforms[] =
{
    { transformUnpack, "unpack" }, // referenced in parser
    { transformToInt, "toint" },
};


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
    reset();
    literals.clear(*this);
}

bool VM::run(VarCRef v)
{
    reset();
    _base = v;
    push(v);
    return exec(1);
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

// templated adapter to get a const Var* from whatever iterator
template<typename T>
struct GetElem;

template<>
struct GetElem<const Var*>
{
    static inline const Var *get(const Var *p) { return p; }
};
template<>
struct GetElem<Var::Map::Iterator>
{
    static inline const Var* get(Var::Map::Iterator it) { return &it->second; }
};

template<typename Iter>
static void filterElements(VarRefs& out, const TreeMem& mem, Iter begin, Iter end, Var::CompareMode cmp, VarCRef literal, const char *keystr, unsigned invert)
{
    typedef typename GetElem<Iter> Get;
    const Var null;
    const VarCRef nullref(mem, &null);
    for(Iter it = begin; it != end; ++it)
    {
        VarCRef elem(mem, Get::get(it));

        // trying to look up key in something that's not a map -> always skip
        if (elem.type() != Var::TYPE_MAP)
            continue;

        VarCRef sub = elem.lookup(keystr);
        // There are two ways to handle this.
        // We could handle things so that "key does not exist" and
        // "key exists but value is null" are two different things.
        // But from a practical perspective, it makes more sense to handle
        // a non-existing key as if the value was null.
        // That way we can do [k != null] to select anything that has a key k.
        if (!sub)
        {
            //continue; // Key doesn't exist, not map, etc -> skip

            sub = nullref; // handle missing key as if the value was null
        }

        Var::CompareResult res = sub.compare(cmp, literal);
        if (res == Var::CMP_RES_NA)
            continue; // Can't be compared, skip

        unsigned success = (res ^ invert) & 1;
        if (success)
            out.push_back(elem);
    }
}

// keep refs in top only when a subkey has operator relation to a literal
void VM::cmd_CheckKey(unsigned param, unsigned lit)
{
    unsigned invert = param & 1;
    unsigned op = (param >> 1) & 7;
    unsigned key = param >> 4;

    StackFrame& top = _topframe();
    size_t wpos = 0;

    VarCRef checklit(*this, &literals[lit]);
    Var::CompareMode cmp = Var::CompareMode(op);

    const char* keystr = literals[key].asCString(*this);
    VarRefs oldrefs;
    std::swap(oldrefs, top.refs);
    const size_t N = oldrefs.size();

    // apply to all values on top

    for(size_t i = 0; i < N; ++i)
    {
        VarCRef elem = oldrefs[i];
        if(const Var *a = elem.v->array())
            filterElements(top.refs, *elem.mem, a, a + elem.v->_size(), cmp, checklit, keystr, invert);
        else if(const Var::Map *m = elem.v->map())
            filterElements(top.refs, *elem.mem, m->begin(), m->end(), cmp, checklit, keystr, invert);
        // else can't select subkey, so drop elem
    }
}

void VM::cmd_PushVar(unsigned param)
{
    // literals and vars use the same VM memory space (*this),
    // so we can just pass a StrRef along
    StrRef key = literals[param].asStrRef();
    StackFrame* frm = _getVar(key);
    assert(frm);
    StackFrame newtop;
    newtop.refs = frm->refs; // copy refs only; frm will stay alive
    stack.push_back(std::move(newtop));
}

void VM::cmd_Transform(unsigned param)
{
    assert(param < Countof(s_transforms));
    // need to keep the old top around until the transform is done
    StackFrame oldfrm = std::move(stack.back());
    stack.pop_back();
    stack.emplace_back();
    // now there's a shiny new top, fill it
    s_transforms[param].func(*this, stack[stack.size() - 1], oldfrm);
    oldfrm.clear(*this);
}

void VM::push(VarCRef v)
{
    StackFrame frm;
    frm.refs.push_back(v);
    stack.push_back(std::move(frm));
}

bool VM::exec(size_t ip)
{
    assert(!stack.empty() && "Must push initial data on the VM stack before starting to execute");
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

            case CM_GETVAR:
                cmd_PushVar(c.param);
                break;

            case CM_TRANSFORM:
                cmd_Transform(c.param);
                break;

            case CM_COMPARE:
                assert(false);
        }
    }
}

void VM::reset()
{
    // clear stack
    while(stack.size())
        _popframe().clear(*this);

    // clear variables
    Var::Map* m = vars.map();
    for (Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
    {
        if(void* p = it->second.asPtr())
        {
            StackFrame* frm = static_cast<StackFrame*>(p);
            frm->~StackFrame();
            this->Free(frm, sizeof(*frm));
        }
    }
    vars.clear(*this);
    _base.v = NULL;
    _base.mem = NULL;
}

const VarRefs& VM::results() const
{
    return stack.back().refs;
}

StackFrame *VM::storeTop(StrRef s)
{
    StackFrame *frm = detachTop();
    Var& val = vars.map()->getOrCreate(*this, s);
    val.setPtr(*this, frm);
    return frm;
}

// alloc new frame and move top to it
StackFrame* VM::detachTop()
{
    void* p = this->Alloc(sizeof(StackFrame));
    return _X_PLACEMENT_NEW(p) StackFrame(std::move(_popframe()));
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

StackFrame* VM::_evalVar(StrRef key, size_t pc)
{
    const size_t stk = stack.size();
    push(_base);
    if (!exec(pc))
    {
        stack.resize(stk);
        return NULL;
    }
    assert(stack.size() + 1 == stk);
    return detachTop();
}

StackFrame* VM::_getVar(StrRef key)
{
    Var* v = vars.lookup(key);
    assert(v);
    StackFrame* frm = NULL;
    switch(v->type())
    {
        case Var::TYPE_PTR:
            frm = static_cast<StackFrame*>(v->asPtr());
            break;
        case Var::TYPE_UINT:
            frm = _evalVar(key, *v->asUint());
            v->setPtr(*this, frm);
            break;
        default:
            assert(false);
    }
    return frm;
}

int GetTransformID(const char* s)
{
    for (size_t i = 0; i < Countof(s_transforms); ++i)
        if (!strcmp(s, s_transforms[i].name))
            return (int)i;
    return -1;
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
