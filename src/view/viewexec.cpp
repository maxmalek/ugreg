#include "viewexec.h"
#include <assert.h>
#include <utility>
#include <sstream>
#include "treemem.h"
#include "util.h"
#include "safe_numerics.h"
#include "debugfunc.h"
#include "mem.h"
#include "json_out.h"

#ifndef NDEBUG
#define DEBUG_PRINT printf
#else
#define DEBUG_PRINT /* crickets */
#endif

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
void addRel(TreeMem& mem, StackFrame& frame, Var&& v, StrRef k)
{
    VarEntry e { VarCRef(mem, (const Var*)(uintptr_t)(frame.store.size())), k };
    frame.refs.push_back(std::move(e));
    frame.store.push_back(std::move(v));
}
void addAbs(TreeMem& mem, StackFrame& frame, Var&& v, StrRef k)
{
    assert(frame.store.size() < frame.store.capacity() && "vector would reallocate");
    frame.store.push_back(std::move(v));
    VarEntry e{ VarCRef(mem, &frame.store.back()), k };
    frame.refs.push_back(std::move(e));
}
void makeAbs(StackFrame& frame)
{
    const Var *base = frame.store.data();
    const size_t n = frame.refs.size();
    for(size_t i = 0; i < n; ++i)
        frame.refs[i].ref.v = base + (uintptr_t)(frame.refs[i].ref.v);
}

// TODO: move transforms to own file?

// unpack arrays and maps, skip anything else
static void transformUnpack(TreeMem& mem, StackFrame& newframe, StackFrame& oldframe)
{
    newframe.store = std::move(oldframe.store);

    const size_t n = oldframe.refs.size();

    // figure out new size after everything is unpacked
    size_t nn = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const VarEntry& src = oldframe.refs[i];
        if(src.ref.v->isContainer())
            nn += src.ref.size();
    }

    newframe.refs.reserve(nn);

    for (size_t i = 0; i < n; ++i)
    {
        const VarEntry& src = oldframe.refs[i];
        switch(src.ref.type())
        {
            case Var::TYPE_ARRAY:
                for(size_t k = 0; k < nn; ++k)
                {
                    VarEntry e { src.ref.at(k), 0 };
                    newframe.refs.push_back(std::move(e));
                }
            break;

            case Var::TYPE_MAP:
            {
                const Var::Map *m = src.ref.v->map_unsafe();
                for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
                {
                    VarEntry e { VarCRef(src.ref.mem, &it->second), it->first };
                    newframe.refs.push_back(std::move(e));
                }
            }
            break;

            default: assert(false); break;
        }
    }
}

static void transformToInt(TreeMem& mem, StackFrame& newframe, StackFrame& oldframe)
{
    const size_t n = oldframe.store.size();
    newframe.store.reserve(n);

    for(size_t i = 0; i < n; ++i)
    {
        VarEntry& src = oldframe.refs[i];
        Var newval;
        switch(src.ref.type())
        {
            case Var::TYPE_INT:
            case Var::TYPE_UINT:
                newframe.refs.push_back(std::move(src));
                continue;

            case Var::TYPE_STRING:
            {
                const PoolStr ps = src.ref.asString();
                s64 val;
                if(!strtoi64NN(&val, ps.s, ps.len).ok())
                    break; // null val
                newval.setInt(mem, val);
            }
            break;

            default: ;
                // null val
        }
        addAbs(mem, newframe, std::move(newval), src.key);
    }

    assert(newframe.refs.size() == oldframe.refs.size());
}

static void transformCompact(TreeMem& mem, StackFrame& newframe, StackFrame& oldframe)
{
    newframe.store = std::move(oldframe.store);
    const size_t n = oldframe.refs.size();
    newframe.refs.reserve(n);

    size_t w = 0;
    for (size_t i = 0; i < n; ++i)
        if(!oldframe.refs[i].ref.isNull())
            oldframe.refs[w++] = oldframe.refs[i];

    oldframe.refs.resize(w);
    newframe.refs = std::move(oldframe.refs);
}

static void transformAsArray(TreeMem& mem, StackFrame& newframe, StackFrame& oldframe)
{
    Var arr;
    const size_t N = oldframe.refs.size();
    Var *a = arr.makeArray(mem, N);
    const Var *mybegin = &oldframe.store.front();
    const Var *myend = &oldframe.store.back();
    for(size_t i = 0; i < N; ++i)
    {
        // We're making an array, so any keys get lost
        VarCRef r = oldframe.refs[i].ref;
        if(r.mem == &mem && mybegin <= r.v && r.v <= myend) // if we own the memory, we can just move the thing
            a[i] = std::move(*const_cast<Var*>(r.v));
        else // but if it's in some other memory space, we must clone it
            a[i] = std::move(r.v->clone(mem, *r.mem));
    }
    newframe.store.reserve(1);
    addAbs(mem, newframe, std::move(arr), 0);
}

static void transformAsMap(TreeMem& mem, StackFrame& newframe, StackFrame& oldframe)
{
    Var mp;
    const size_t N = oldframe.refs.size();
    Var::Map* m = mp.makeMap(mem, N);
    const Var* mybegin = &oldframe.store.front();
    const Var* myend = &oldframe.store.back();
    for (size_t i = 0; i < N; ++i)
    {
        // If the element didn't originally come from a map, drop it.
        // Since we don't know the key to save this under there's nothing we can do
        // TODO: error out instead?
        if(!oldframe.refs[i].key)
            continue;

        VarCRef r = oldframe.refs[i].ref;
        StrRef k = oldframe.refs[i].key;
        if (r.mem == &mem && mybegin <= r.v && r.v < myend) // if we own the memory, we can just move the thing
            m->put(mem, k, std::move(*const_cast<Var*>(r.v)));
        else // but if it's in some other memory space, we must clone it
        {
            PoolStr ps = r.mem->getSL(k);
            assert(ps.s);
            Var& dst = m->putKey(mem, ps.s, ps.len);
            dst = std::move(r.v->clone(mem, *r.mem));
        }
    }
    newframe.store.reserve(1);
    addAbs(mem, newframe, std::move(mp), 0);
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
    { transformCompact, "compact" },
    { transformAsArray, "array" },
    { transformAsMap, "map" },
};

VM::VM()
{
}

VM::~VM()
{
    reset();
    literals.clear(*this);

    if(Var::Map* m = evals.map())
        for (Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
            if (void* p = it->second.asPtr())
            {
                StackFrame* frm = static_cast<StackFrame*>(p);
                frm->~StackFrame();
                this->Free(frm, sizeof(*frm));
            }
    evals.clear(*this);
}

void VM::init(const Executable& ex, const EntryPoint* eps, size_t numep)
{
    cmds = ex.cmds; // make a copy

    evals.makeMap(*this);
    if (numep)
    {
        VarRef evalmap(*this, &evals);
        for (size_t i = 0; i < numep; ++i)
        {
            DEBUG_PRINT("Eval entrypoint: [%s] = %u\n", eps[i].name.c_str(), (unsigned)eps[i].idx);
            evalmap[eps[i].name.c_str()].v->setUint(*this, eps[i].idx);
        }
    }

    // literals are only ever referred to by index, copy those as well
    const size_t N = ex.literals.size();
    Var* a = literals.makeArray(*this, N);
    for (size_t i = 0; i < N; ++i)
        a[i] = std::move(ex.literals[i].clone(*this, *ex.mem));
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
    VarEntry * const ain = top.refs.data();
    VarEntry *aout = ain;

    // TODO: make variant for json ptr?
    assert(ps.s[0] != '/');

    for (size_t i = 0; i < N; ++i)
        if(const Var *sub = ain[i].ref.lookup(ps.s)) // NULL if not map
        {
            aout->ref.mem = ain[i].ref.mem;
            aout->ref.v = sub;
            aout->key = ain[i].key;
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
    static inline const StrRef key(const Var *p) { return 0; }
};
template<>
struct GetElem<Var::Map::Iterator>
{
    static inline const Var* get(Var::Map::Iterator it) { return &it->second; }
    static inline const StrRef key(Var::Map::Iterator it) { return it->first; }
};

template<typename Iter>
static void filterElements(VarRefs& out, const TreeMem& mem, Iter begin, Iter end, Var::CompareMode cmp, const VarEntry *values, size_t numvalues, const char *keystr, unsigned invert)
{
    typedef typename GetElem<Iter> Get;
    for(Iter it = begin; it != end; ++it)
    {
        const VarEntry e { VarCRef(mem, Get::get(it)), Get::key(it) };

        // trying to look up key in something that's not a map -> always skip
        if (e.ref.type() != Var::TYPE_MAP)
            continue;

        VarCRef sub = e.ref.lookup(keystr);
        // There are two ways to handle this.
        // We could handle things so that "key does not exist" and
        // "key exists but value is null" are two different things.
        // But from a practical perspective, it makes more sense to handle
        // a non-existing key as if the value was null.
        // That way we can do [k != null] to select anything that has a key k.
        if (!sub)
        {
            //continue; // Key doesn't exist, not map, etc -> skip

            sub.makenull(); // handle missing key as if the value was null
        }

        for(size_t k = 0; k < numvalues; ++k)
        {
            Var::CompareResult res = sub.compare(cmp, values[k].ref);
            if (res == Var::CMP_RES_NA)
                continue; // Can't be compared, skip

            unsigned success = (res ^ invert) & 1;
            if (success)
            {
                out.push_back(e);
                break;
            }
            // else keep checking
        }
    }
}

void VM::cmd_Filter(unsigned param)
{
    const unsigned invert = param & 1;
    const unsigned op = (param >> 1) & 7;
    const unsigned key = param >> 4;
    const Var::CompareMode cmp = Var::CompareMode(op);

    StackFrame vs = _popframe(); // check new top vs. this
    StackFrame& top = _topframe();

    const char* keystr = literals[key].asCString(*this);
    VarRefs oldrefs;
    std::swap(oldrefs, top.refs);
    const size_t N = oldrefs.size();

    DEBUG_PRINT("Filter %u refs using %u refs\n", (unsigned)N, (unsigned)vs.refs.size());

    // apply to all values on top
    for (size_t i = 0; i < N; ++i)
    {
        VarEntry e = oldrefs[i];
        if (const Var* a = e.ref.v->array())
            filterElements(top.refs, *e.ref.mem, a, a + e.ref.v->_size(), cmp, vs.refs.data(), vs.refs.size(), keystr, invert);
        else if (const Var::Map* m = e.ref.v->map())
            filterElements(top.refs, *e.ref.mem, m->begin(), m->end(), cmp, vs.refs.data(), vs.refs.size(), keystr, invert);
        // else can't select subkey, so drop elem
    }

    DEBUG_PRINT("... %u refs passed the filter\n", (unsigned)top.refs.size());
}

void VM::cmd_Keysel(unsigned param)
{
    const unsigned keep = param & 1;
    const unsigned index = param >> 1u;
    Var& lit = literals[index];
    assert(lit.type() == Var::TYPE_MAP);

    Var::Map *Lm = lit.map();
    StackFrame& top = _topframe();

    StackFrame newtop;
    TreeMem * const mymem = this;
    if(keep)
    {
        for(const VarEntry& e : top.refs)
            if(const Var::Map *src = e.ref.v->map())
            {
                Var mm;
                Var::Map *newmap = mm.makeMap(*this);
                for(Var::Map::Iterator it = Lm->begin(); it != Lm->end(); ++it)
                {
                    StrRef readk = it->second.asStrRef();
                    StrRef writek = it->first;
                    const Var *x;
                    if(mymem != e.ref.mem)
                    {
                        PoolStr ps = this->getSL(readk);
                        readk = e.ref.mem->lookup(ps.s, ps.len);
                        if(!readk)
                            continue;
                    }
                    if(const Var *x = src->get(readk))
                        newmap->put(*this, it->first, std::move(x->clone(*this, *e.ref.mem)));
                }
                addRel(*this, newtop, std::move(mm), e.key);
            }
    }
    else
    {
        for (const VarEntry& e : top.refs)
            if (const Var::Map* src = e.ref.v->map())
            {
                Var mm;
                Var::Map* newmap = mm.makeMap(*this);
                for (Var::Map::Iterator it = src->begin(); it != src->end(); ++it)
                {
                    StrRef k = it->first;
                    PoolStr ps = e.ref.mem->getSL(k);
                    StrRef myk = this->putNoRefcount(ps.s, ps.len);
                    assert(myk);
                    if(!Lm->get(myk))
                        newmap->getOrCreate(*this, myk) = std::move(it->second.clone(*mymem, *e.ref.mem));
                }
                addRel(*this, newtop, std::move(mm), e.key);
            }
    }

    makeAbs(newtop);
    top = std::move(newtop);
}

// keep refs in top only when a subkey has operator relation to a literal
void VM::cmd_CheckKeyVsSingleLiteral(unsigned param, unsigned lit)
{
    const unsigned invert = param & 1;
    const unsigned op = (param >> 1) & 7;
    const unsigned key = param >> 4;
    const Var::CompareMode cmp = Var::CompareMode(op);

    const VarEntry checklit { VarCRef(*this, &literals[lit]), 0 };
    StackFrame& top = _topframe();

    const char* keystr = literals[key].asCString(*this);
    VarRefs oldrefs;
    std::swap(oldrefs, top.refs);
    const size_t N = oldrefs.size();

    // apply to all values on top

    for(size_t i = 0; i < N; ++i)
    {
        VarEntry e = oldrefs[i];
        if(const Var *a = e.ref.v->array())
            filterElements(top.refs, *e.ref.mem, a, a + e.ref.v->_size(), cmp, &checklit, 1, keystr, invert);
        else if(const Var::Map *m = e.ref.v->map())
            filterElements(top.refs, *e.ref.mem, m->begin(), m->end(), cmp, &checklit, 1, keystr, invert);
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
    VarEntry e { v, 0 };
    frm.refs.push_back(std::move(e));
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
                cmd_CheckKeyVsSingleLiteral(c.param, c.param2);
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


            case CM_GETVAR:
                cmd_PushVar(c.param);
                break;

            case CM_TRANSFORM:
                cmd_Transform(c.param);
                break;

            case CM_FILTER:
                cmd_Filter(c.param);
                break;

            case CM_KEYSEL:
                cmd_Keysel(c.param);

            case CM_DONE:
                return true;
        }
    }
}

void VM::reset()
{
    // clear stack
    while(stack.size())
        _popframe().clear(*this);

    _base.v = NULL;
    _base.mem = NULL;
}

const VarRefs& VM::results() const
{
    return stack.back().refs;
}

bool VM::exportResult(Var& dst) const
{
    return false;
}

StackFrame *VM::storeTop(StrRef s)
{
    StackFrame *frm = detachTop();
    Var& val = evals.map()->getOrCreate(*this, s);
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
    assert(stack.size() == stk + 1);
    return detachTop();
}

StackFrame* VM::_getVar(StrRef key)
{
    Var* v = evals.lookup(key);
    if(!v)
    {
        printf("Attempt to eval [%s], but does not exist\n",
            this->getS(key));
        return NULL;
    }
    StackFrame* frm = NULL;
    switch(v->type())
    {
        case Var::TYPE_PTR:
            frm = static_cast<StackFrame*>(v->asPtr());
            DEBUG_PRINT("Eval [%s] cached, frame refs size = %u\n", this->getS(key), (unsigned)frm->refs.size());
            break;
        case Var::TYPE_UINT:
        {
            size_t ip = *v->asUint();
            v->clear(*this); // detect self-referencing
            DEBUG_PRINT("Eval [%s], not stored, exec ip = %u\n", this->getS(key), (unsigned)ip);
            frm = _evalVar(key, ip);
            v->setPtr(*this, frm);
            DEBUG_PRINT("Eval [%s] done, frame refs size = %u\n", this->getS(key), (unsigned)frm->refs.size());
            break;
        }
        case Var::TYPE_NULL:
            printf("Eval [%s] not stored and self-referencing, abort\n", this->getS(key));
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

const char* GetTransformName(int id)
{
    return id >= 0 && id < Countof(s_transforms)
        ? s_transforms[id].name
        : NULL;
}

Executable::Executable(TreeMem& mem)
    : mem(&mem)
{
}

Executable::Executable(Executable&& o) noexcept
    : cmds(std::move(o.cmds))
    , literals(std::move(o.literals))
    , mem(o.mem)
{
    o.mem = NULL;
}

Executable::~Executable()
{
    clear();
}
void Executable::clear()
{
    if(mem)
    {
        for(size_t i = 0; i < literals.size(); ++i)
            literals[i].clear(*mem);
        literals.clear();
        mem = NULL;
    }
}

// ----------------------------------------------

static const char *s_opcodeNames[] =
{
    "GETKEY",
    "GETVAR",
    "TRANSFORM",
    "FILTER",
    "LITERAL",
    "DUP",
    "CHECKKEY",
    "KEYSEL",
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
        os << " [" << i << "] ";
        os << s_opcodeNames[c.type];

        switch(cmds[i].type)
        {
            case CM_DUP:
            case CM_DONE:
                break; // do nothing
            case CM_GETVAR:
                os << ' ' << literals[c.param].asCString(*mem);
                break;
            case CM_GETKEY:
            case CM_LITERAL:
                os << ' ';
                varToString(os, VarCRef(mem, &literals[c.param]));
                break;
            case CM_TRANSFORM:
                os << " " << GetTransformName(c.param) << " (func #" << c.param << ")";
                break;
            case CM_FILTER:
            {
                unsigned key = c.param >> 4;
                os << ' ';
                oprToStr(os, c.param & 0xf);
                os << " (key: " << literals[key].asCString(*mem) << ")";
            }
            break;

            case CM_CHECKKEY:
            {
                unsigned key = c.param >> 4;
                os << " [ " << literals[key].asCString(*mem);
                oprToStr(os, c.param & 0xf);
                os << ' ';
                varToString(os, VarCRef(mem, &literals[c.param2]));
                os << " ]";
            }
            break;

            case CM_KEYSEL:
            {
                unsigned index = c.param >> 1u;
                unsigned keep = c.param & 1;
                os << (keep ? " KEEP " : " DROP ") << dumpjson(VarCRef(mem, &literals[index]), false);
            }
            break;

            default:
                assert(false);

        }

        out.push_back(os.str());
    }
    {
        std::ostringstream os;
        os << "--- Literals[" << literals.size() << "]---";
        out.push_back(os.str());
    }
    for(size_t i = 0; i < literals.size(); ++i)
    {
        std::ostringstream os;
        os << " [" << i << "] = ";
        varToString(os, VarCRef(mem, &literals[i]));
        out.push_back(os.str());
    }

    return n;
}

} // end namespace view
