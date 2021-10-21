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
#include "viewxform.h"

#ifndef NDEBUG
#define DEBUG_PRINT printf
#else
#define DEBUG_PRINT /* crickets */
#endif

namespace view {

void StackFrame::addRel(TreeMem& mem, Var&& v, StrRef k)
{
    VarEntry e{ VarCRef(mem, (const Var*)(uintptr_t)(store.size())), k, v.getExtra() };
    refs.push_back(std::move(e));
    store.push_back(std::move(v));
}
void StackFrame::addAbs(TreeMem& mem, Var&& v, StrRef k)
{
    assert(store.size() < store.capacity() && "vector would reallocate");
    store.push_back(std::move(v));
    VarEntry e{ VarCRef(mem, &store.back()), k, store.back().getExtra() };
    refs.push_back(std::move(e));
}
void StackFrame::makeAbs()
{
    const Var* base = store.data();
    const size_t n = refs.size();
    for (size_t i = 0; i < n; ++i)
        refs[i].ref.v = base + (uintptr_t)(refs[i].ref.v);
}

void StackFrame::clear(TreeMem& mem)
{
    const size_t n = store.size();
    for(size_t i = 0; i < n; ++i)
        store[i].clear(mem);
    store.clear();
    refs.clear();
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

void VM::_freeStackFrame(void *p)
{
    StackFrame* frm = static_cast<StackFrame*>(p);
    frm->~StackFrame();
    this->Free(frm, sizeof(*frm));
}

VM::~VM()
{
    reset();
    literals.clear(*this);

    if(Var::Map* m = evals.map())
        for (Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
            if (void* p = it.value().asPtr())
                _freeStackFrame(p);
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

VarRef VM::makeVar(const char* name, size_t len)
{
    // if there was previously a stackframe under that name, kill it
    Var& dst = evals.map()->putKey(*this, name, len);
    if(void *prev = dst.asPtr())
        _freeStackFrame(prev);

    void* p = this->Alloc(sizeof(StackFrame));
    StackFrame *sf = _X_PLACEMENT_NEW(p) StackFrame;
    dst.setPtr(*this, sf);
    sf->addAbs(*this, std::move(Var()), 0);
    return VarRef(this, &sf->store[0]);
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
            if(const Var::Extra *extra = sub->getExtra())
                aout->extra = extra;
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
    static inline const Var* get(Var::Map::Iterator it) { return &it.value(); }
    static inline const StrRef key(Var::Map::Iterator it) { return it.key(); }
};

static void filterElement(VarRefs& out, const VarEntry& e, Var::CompareMode cmp, const VarEntry* values, size_t numvalues, const char* keystr, unsigned invert)
{
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

    for (size_t k = 0; k < numvalues; ++k)
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
    }
}

/*template<typename Iter>
static void filterElementsInObject(VarRefs& out, const TreeMem& mem, Iter begin, Iter end, Var::CompareMode cmp, const VarEntry *values, size_t numvalues, const char *keystr, unsigned invert)
{
    typedef typename GetElem<Iter> Get;
    for(Iter it = begin; it != end; ++it)
    {
        const VarEntry e { VarCRef(mem, Get::get(it)), Get::key(it) };

        // trying to look up key in something that's not a map -> always skip
        if (e.ref.type() != Var::TYPE_MAP)
            continue;

        filterElement(out, e, cmp, values, numvalues, keystr, invert);
    }
}*/

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
        const VarEntry& e = oldrefs[i];
        filterElement(top.refs, e, cmp, vs.refs.data(), vs.refs.size(), keystr, invert);
        // else can't select subkey, so drop elem
    }

    DEBUG_PRINT("... %u refs passed the filter\n", (unsigned)top.refs.size());
}

void VM::cmd_Keysel(unsigned param)
{
    const unsigned keep = param & 1;
    const unsigned index = param >> 1u;
    const Var& lit = literals[index];
    assert(lit.type() == Var::TYPE_MAP);

    const Var::Map *Lm = lit.map();
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
                    StrRef readk = it.value().asStrRef();
                    if(mymem != e.ref.mem)
                    {
                        PoolStr ps = this->getSL(readk);
                        readk = e.ref.mem->lookup(ps.s, ps.len);
                        if(!readk)
                            continue;
                    }
                    if(const Var *x = src->get(readk))
                        newmap->put(*this, it.key(), std::move(x->clone(*this, *e.ref.mem)));
                }
                newtop.addRel(*this, std::move(mm), e.key);
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
                    StrRef k = it.key();
                    PoolStr ps = e.ref.mem->getSL(k);
                    StrRef myk = this->putNoRefcount(ps.s, ps.len);
                    assert(myk);
                    if(!Lm->get(myk))
                        newmap->getOrCreate(*this, myk) = std::move(it.value().clone(*mymem, *e.ref.mem));
                }
                newtop.addRel(*this, std::move(mm), e.key);
            }
    }

    newtop.makeAbs();
    top.clear(*this);
    top = std::move(newtop);
}

void VM::cmd_Select(unsigned param)
{
    StackFrame& top = _topframe();
    StackFrame newtop;

    const Var& lit = literals[param];

    switch(lit.type())
    {
        case Var::TYPE_RANGE:
        {
            const size_t RL = lit.size();
            const Var::Range * const rbase = lit.asRange();
            for(size_t j = 0; j < top.refs.size(); ++j)
            {
                const VarEntry& e = top.refs[j];                // for each thing on stack...
                if(const Var *asrc = e.ref.v->array())
                {
                    size_t asize = e.ref.size();
                    std::vector<Var> tmp;
                    if(asize)
                    {
                        for(size_t k = 0; k < RL; ++k)
                        {
                            const Var::Range& ra = rbase[k];    // ...for each component of the range...
                            const size_t last = std::min(ra.last, asize);
                            for(size_t i = ra.first; i < last; ++i)
                                tmp.push_back(std::move(asrc[i].clone(*this, *e.ref.mem))); // ... add an element
                        }
                    }
                    Var newa;
                    Var *adst = newa.makeArray(*this, tmp.size());
                    std::move(tmp.begin(), tmp.end(), adst);
                    newtop.addRel(*this, std::move(newa), e.key);
                }
            }
        }
        break;

        default:
            assert(false);
            break;
    }

    newtop.makeAbs();
    top.clear(*this);
    top = std::move(newtop);
}

void VM::cmd_SelectStack()
{
    StackFrame sel = _popframe();
    StackFrame& top = _topframe();
    assert(false); // TODO WRITE ME
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
        filterElement(top.refs, e, cmp, &checklit, 1, keystr, invert);
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
                break;

            case CM_SELECT:
                cmd_Select(c.param);
                break;

            case CM_SELECTSTACK:
                cmd_SelectStack();

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
    "SELECT",
    "SELECTSTACK",
    "CONCAT",
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
            case CM_SELECT:
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
