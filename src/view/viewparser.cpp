#include "viewparser.h"
#include <vector>
#include <string>
#include <assert.h>
#include "util.h"
#include "safe_numerics.h"
#include "treemem.h"
#include "viewexec.h"

namespace view {


enum { MAX_OP_LEN = 2 };
struct OpEntry
{
    const char text[MAX_OP_LEN + 1]; // + \0
    Var::CompareMode op;
    unsigned invert;
};

// need to check the longer ones first so that there's no collision (ie. checking < first and then returning, even though it was actually <=)
// negation is handled in _parseOp()
static const OpEntry ops[] =
{
    // anything
    { "==", Var::CMP_EQ, 0 },
    { "=",  Var::CMP_EQ, 0 },
    { "<>", Var::CMP_EQ, 1 }, // eh why not

    // numeric (or lexical check for strings)
    { ">=", Var::CMP_LT, 1 },
    { "<=", Var::CMP_GT, 1 },
    { "<",  Var::CMP_LT, 0 },
    { ">",  Var::CMP_GT, 0 },

    // substring
    { "??",  Var::CMP_CONTAINS, 0 },
    { "?<",  Var::CMP_STARTSWITH, 0 },
    { "?>",  Var::CMP_ENDSWITH, 0 },
};

struct ParserState
{
    const char *ptr;
    size_t cmdidx;
    size_t literalidx;
    size_t stridx;
};

class Parser
{
public:
    Parser(Executable& exe)
        : ptr(NULL), mem(exe.mem), exec(exe) {}

    ParserState snapshot() const;
    void rewind(const ParserState& ps);
    size_t parse(const char *s); // returns index where execution of the parsed block starts, or 0 on error

private:

    bool _parseLookup(bool ignoreStartSlash = false);
    bool _parseKey(bool ignoreStartSlash = false);
    bool _parseNum(Var& v);
    bool _parseStr(Var& v);
    bool _parseNull();
    bool _parseBool(Var& v);
    size_t _parseVerbatim(const char *in); // returns length of match if matched, otherwise 0
    bool _parseLiteral(Var& v);
    bool _parseValue(); // literal or eval
    bool _parseDecimal(u64& i);
    bool _addMantissa(double& f, u64 i);
    bool _parseSelector();
    bool _parseSelection();
    bool _parseSimpleSelection();
    bool _parseEval();
    bool _parseSimpleEval();
    bool _parseExtendedEval();
    bool _parseIdent(Var& id); // write identifier name to id
    bool _parseIdentOrStr(Var& id);
    bool _skipSpace(bool require = false);
    bool _eat(char c);
    bool _isSep() const; // next char is separator, ie. []/,space,etc or end of string
    bool _parseOp(Cmd& op);

    void _emit(CmdType cm, unsigned param, unsigned param2 = 0);
    unsigned _addLiteral(Var&& lit); // return index into literals table
    unsigned _emitPushVarRef(Var&& v);
    unsigned _emitPushLiteral(Var&& v);
    unsigned _emitGetKey(Var&& v);
    void _emitCheckKey(Var&& key, Var&& lit, unsigned opparam);

    const char *ptr;
    TreeMem& mem;
    Executable& exec;

};

size_t parse(Executable& exe, const char *s)
{
    Parser p(exe);
    return p.parse(s);
}

static inline bool _IsSep(char c)
{
    return c == '[' || c == ']' || c == '/' || c == ' ' || c == '\t' || !c;
}

bool Parser::_isSep() const
{
    return _IsSep(*ptr);
}


struct ParserTop
{
public:
    ParserTop(Parser& p) : state(p.snapshot()), _p(p), _accepted(false) {}
    ~ParserTop() { if(!_accepted) _p.rewind(state); }
    bool accept() { _accepted = true; return true; }
    const ParserState state;
private:
    Parser& _p;
    bool _accepted;
};

ParserState Parser::snapshot() const
{
    ParserState ps;
    ps.ptr = ptr;
    ps.cmdidx = exec.cmds.size();
    ps.literalidx = exec.literals.size();
    return ps;
}

void Parser::rewind(const ParserState& ps)
{
    ptr = ps.ptr;
    assert(ps.cmdidx <= exec.cmds.size());
    exec.cmds.resize(ps.cmdidx);
    assert(ps.literalidx <= exec.literals.size());
    exec.literals.resize(ps.literalidx);
}

size_t Parser::parse(const char *s)
{
    ptr = s;
    size_t start = exec.cmds.size();
    if(!start) // since we return 0 only on error, add at least 1 dummy opcode
    {
        _emit(CM_DONE, 0);
        ++start;
    }

    if(_parseLookup() && _skipSpace() && *ptr == 0)
    {
        _emit(CM_DONE, 0);
        return start;
    }
    return 0;
}

// 1337
// 3.141596
// -42
// -123.456
bool Parser::_parseNum(Var& v)
{
    ParserTop top(*this);
    u64 i;

    bool neg = _eat('-');

    if(!_parseDecimal(i))
        return false;

    if (_isSep())
    {
        if (!neg) // it's just unsigned, easy
            v.setUint(mem, i);
        else if (isValidNumericCast<s64>(i)) // but does it fit?
            v.setInt(mem, -s64(i));
        else
            return false;
    }
    else if(_eat('.'))
    {
        double d;
        if(!_addMantissa(d, i))
            return false;
        v.setFloat(mem, !neg ? d : -d);
    }
    else
        return false;

    return top.accept();
}

// FIXME: make this handle both ' and " and handle escapes inside
bool Parser::_parseStr(Var& v)
{
    const char *s = ptr;
    if(*s++ != '\'')
        return false;

    const char * const begin = s;
    bool esc = false;
    for(char c; (c = *s); ++s)
    {
        if(c == '\'' && !esc)
        {
            v.setStr(mem, begin, s - begin);
            ptr = s + 1; // skip closing '
            return true;
        }

        esc = c == '\\';
    }
    return false;
}

bool Parser::_parseNull()
{
    ParserTop top(*this);
    return _parseVerbatim("null") && top.accept();
}

bool Parser::_parseBool(Var& v)
{
    ParserTop top(*this);
    bool val = false;
    bool ok = (val = _parseVerbatim("true")) || _parseVerbatim("false");
    if (ok)
        v.setBool(mem, val);
    return ok && top.accept();
}

size_t Parser::_parseVerbatim(const char *in)
{
    assert(*in);
    const char * const s = ptr;
    for (size_t k = 0;; ++k)
    {
        char c = *in++;
        if (!c) // all matched
        {
            ptr = s + k;
            return k;
        }
        if (s[k] != c)
            break;
    }
    return 0;
}

bool Parser::_parseLiteral(Var& v)
{
    return _parseStr(v) || _parseNum(v) || _parseBool(v) || _parseNull();
}

bool Parser::_parseValue()
{
    if(_parseEval())
        return true;
    Var v;
    if(_parseLiteral(v))
    {
        _emitPushLiteral(std::move(v));
        return true;
    }
    return false;
}

bool Parser::_parseDecimal(u64& i)
{
    NumConvertResult nr = strtou64NN(&i, ptr);
    bool ok = nr.ok();
    if(ok)
        ptr += nr.used;
    return ok;
}

bool Parser::_addMantissa(double& f, u64 i)
{
    const char *s = ptr;
    u64 m;
    NumConvertResult nr = strtou64NN(&m, s);
    if(!nr.ok())
        return false;

    s += nr.used;
    unsigned div = 1;
    do
        div *= 10;
    while(--nr.used);

    f = double(i) + (double(m) / double(div));
    ptr = s;
    return true;
}

// /path/to/thing[...]/blah
// -> any number of keys and selectors
bool Parser::_parseLookup(bool ignoreStartSlash)
{
    ParserTop top(*this);
    size_t n = 0;
    while(_parseKey() || _parseSelector())
    {
        ++n;
        ignoreStartSlash = false;
    }
    return n && top.accept();
}

// /key
bool Parser::_parseKey(bool ignoreStartSlash)
{
    const char *begin = ptr;
    if(begin[0] == '/')
        ++begin;
    else if(!ignoreStartSlash)
        return false;

    const char *s = begin;
    for(char c; (c = *s) && c != '/' && c != '['; )
        ++s;

    // TODO: handle numeric keys?
    Var k(mem, begin, s - begin);
    _emitGetKey(std::move(k));

    ptr = s;
    return true; // zero-length key is okay and is the empty string
}

// [expr]
bool Parser::_parseSelector()
{
    ParserTop top(*this);
    return _eat('[') && _parseSelection() && _eat(']') && top.accept();
}

// $ident
// ${...}
bool Parser::_parseEval()
{
    ParserTop top(*this);
    return _eat('$') && (_parseExtendedEval() || _parseSimpleEval()) && top.accept();
}

// $name
// lookup ident
bool Parser::_parseSimpleEval()
{
    Var id;
    bool ok = _parseIdent(id);
    if(ok)
    {
        unsigned lit = _addLiteral(std::move(id));
        _emit(CM_GETVAR, lit);
    }
    return ok;
}

// {ident}
// {ident followed by modifiers}
// {/lookup/somewhere}
// {/{lookup/somewhere} followed by modifiers} -- in case the lookup contains spaces // TODO
bool Parser::_parseExtendedEval()
{
    ParserTop top(*this);
    Var id;
    if(_eat('{') && _skipSpace() && (_parseIdent(id) /*|| _parseLookup()*/))
    {
        _emitPushVarRef(std::move(id));
        // all following things are transform names
        while(_skipSpace() && _parseIdent(id))
        {
            unsigned tr = GetTransformID(id.asCString(mem));
            _emit(CM_TRANSFORM, tr);
        }
    }
    return _skipSpace() && _eat('}') && top.accept();
}

bool Parser::_parseIdent(Var& id)
{
    const char *s = ptr;
    char c;
    while((c = *s) && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'))
        ++s;

    bool ok = s != ptr;
    if(ok)
    {
        id.setStr(mem, ptr, s - ptr);
        ptr = s;
    }
    return ok;
}

bool Parser::_parseIdentOrStr(Var& id)
{
    return _parseIdent(id) || (_parseLiteral(id) && id.type() == Var::TYPE_STRING);
}

bool Parser::_skipSpace(bool require)
{
    const char *s = ptr;
    for(char c; (c = *s) && (c == ' ' || c == '\t'); )
        ++s;
    bool skipped = s != ptr;
    ptr = s;
    return !require || skipped;
}

bool Parser::_eat(char c)
{
    bool eq = c == *ptr;
    ptr += eq;
    return eq;
}

// anything inside [], can be:
// *              -- unpack array or map values
// name=literal   -- key check
// only simple ops for now, no precedence, no grouping with braces
bool Parser::_parseSelection()
{
    ParserTop top(*this);
    Var id;
    _skipSpace();

    bool ok = false;
    if(_eat('*'))
    {
        _emit(CM_TRANSFORM, GetTransformID("unpack"));
        ok = true;
    }
    else if(_parseSimpleSelection())
    {
        ok = true;
    }

    return ok &&  _skipSpace() && top.accept();
}

// inside []:
// name=<literal>
// name=$var
// '/name with spaces'=..
bool Parser::_parseSimpleSelection()
{
    ParserTop top(*this);
    Var id, lit;
    Cmd op;
    bool ok = false;
    if(_parseIdentOrStr(id) && _skipSpace() && _parseOp(op) && _skipSpace())
    {
        if(_parseLiteral(lit))
        {
            _emitCheckKey(std::move(id), std::move(lit), op.param);
            ok = true;
        }
        /*else
        {
            // FIXME: this is probably broken
            unsigned lit = _addLiteral(std::move(id));
            _emit(CM_GETKEY, lit);
            _emitPushVarRef(std::move(id));
            if(!_parseValue())
                return false;
            exec.cmds.push_back(op);
        }*/

    }
    id.clear(mem);
    lit.clear(mem);
    return ok && _skipSpace() && top.accept();
}

bool Parser::_parseOp(Cmd& cm)
{
    ParserTop top(*this);
    unsigned invert = _eat('!'); // universal negation -- just stick ! in front of it
    for(size_t i = 0; i < Countof(ops); ++i)
    {
        const char *os = ops[i].text;
        if(size_t n = _parseVerbatim(os))
        {
            cm.type = CM_COMPARE;
            cm.param = (ops[i].op << 1) | (invert ^ ops[i].invert);
            return top.accept();
        }
    }
    return false;
}

void Parser::_emit(CmdType cm, unsigned param, unsigned param2)
{
    Cmd c { cm, param, param2 };
    exec.cmds.push_back(c);
}

unsigned Parser::_addLiteral(Var&& lit)
{
    size_t sz = exec.literals.size();
    exec.literals.push_back(std::move(lit));
    return (unsigned)sz;
}

unsigned Parser::_emitPushVarRef(Var&& v)
{
    assert(v.type() == Var::TYPE_STRING);
    unsigned lit = _addLiteral(std::move(v));
    _emit(CM_GETVAR, lit);
    return lit;
}

unsigned Parser::_emitPushLiteral(Var&& v)
{
    unsigned lit = _addLiteral(std::move(v));
    _emit(CM_LITERAL, lit);
    return lit;
}

unsigned Parser::_emitGetKey(Var&& v)
{
    assert(v.type() == Var::TYPE_STRING);
    unsigned lit = _addLiteral(std::move(v));
    _emit(CM_GETKEY, lit);
    return lit;
}

void Parser::_emitCheckKey(Var&& key, Var&& lit, unsigned opparam)
{
    assert(key.type() == Var::TYPE_STRING);
    assert((opparam >> 4) == 0); // if this fires we'd mix up bits with kidx below
    unsigned kidx = _addLiteral(std::move(key));
    unsigned litidx = _addLiteral(std::move(lit));
    _emit(CM_CHECKKEY, opparam | (kidx << 4), litidx);
}

} // end namespace view
