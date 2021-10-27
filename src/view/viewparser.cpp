#include "viewparser.h"
#include <vector>
#include <string>
#include <assert.h>
#include <sstream>
#include "util.h"
#include "safe_numerics.h"
#include "treemem.h"
#include "viewexec.h"

namespace view {

// JSON uses \ as escape char. And this here code is wrapped inside of JSON.
// So that's out because double-escaping things sucks.
// Lua uses % as escape, but that is used for htmlencoding chars (' ' -> %20),
// so that gets in the way when testing queries in the browser.
// ; should be harmless and not really occur in normal query text, so we use that.
// Mode of operation: ; is skipped, the char after loses its special meaning.
// Use ;; to emit a single ;
static const char ESC_CHAR = ';';


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

    // numeric
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
        : ptr(NULL), maxptr(NULL), mem(*exe.mem), exec(exe) {}

    ParserState snapshot() const;
    void rewind(const ParserState& ps);
    size_t parse(const char *s); // returns index where execution of the parsed block starts, or 0 on error

    bool _parseExpr(char close);
    bool _parseQuery();
    bool _parseQueryBody();
    bool _parseUnquotedText(char close);
    bool _parseLookupRoot();
    bool _parseLookupNext();
    bool _parseKey();
    bool _parseNum(Var& v);
    bool _parseTextUntil(Var& v, char close);
    bool _parseTextUntilAnyOf(Var& v, const char* close);
    bool _parseTextUntilAnyOf(Var& v, const char *close, size_t n);
    bool _parseStr(Var& v);
    bool _parseNull();
    bool _parseBool(Var& v);
    size_t _parseVerbatim(const char *in); // returns length of match if matched, otherwise 0
    bool _parseLiteral(Var& v);
    bool _parseValue(); // literal or eval
    bool _parseDecimal(u64& i);
    bool _parseSize(size_t& i);
    bool _addMantissa(double& f, u64 i);
    bool _parseSelector();
    bool _parseSelection();
    bool _parseKeyCmp();
    bool _parseKeySel();
    bool _parseKeySelEntry(Var::Map& m, bool allowRename);
    bool _parseEval();
    bool _parseSimpleEval();
    bool _parseExtendedEval();
    bool _parseIdent(Var& id); // write identifier name to id (as string)
    bool _parseIdentOrStr(Var& id);

    bool _parseRange(Var& r);
    bool _parseRangeEntry(std::vector<Var::Range>& rs);
    bool _skipSpace(bool require = false);
    bool _eat(char c);
    bool _parseBinOp(Cmd& op);

    void _emit(CmdType cm, unsigned param, unsigned param2 = 0);
    unsigned _addLiteral(Var&& lit); // return index into literals table
    unsigned _emitPushVarRef(Var&& v);
    unsigned _emitPushLiteral(Var&& v);
    unsigned _emitGetKey(Var&& v);
    void _emitCheckKey(Var&& key, Var&& lit, unsigned opparam);

    const char *ptr;
    const char *maxptr; // for error reporting only
    TreeMem& mem;
    Executable& exec;
};

size_t parse(Executable& exe, const char *s, std::string& error)
{
    Parser p(exe);
    size_t res = p.parse(s);
    if(!res)
    {
        if(p.maxptr)
        {
            size_t pos = p.maxptr - s;
            std::ostringstream os;
            os << s << "\n";
            for(size_t i = 0; i < pos; ++i)
                os << ' ';
            os << "^-- Parse error here\n";
            error += os.str();
        }
        else
             error += "? Parse error somewhere\n";
    }
    return res;
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
    maxptr = std::max(maxptr, ps.ptr);
    ptr = ps.ptr;
    assert(ps.cmdidx <= exec.cmds.size());
    exec.cmds.resize(ps.cmdidx);
    assert(ps.literalidx <= exec.literals.size());
    while(exec.literals.size() > ps.literalidx)
    {
        exec.literals.back().clear(mem);
        exec.literals.pop_back();
    }
}

size_t Parser::parse(const char *s)
{
    ptr = s; // this must be done before creating top
    ParserTop top(*this);

    size_t start = exec.cmds.size();
    if(!start) // since we return 0 only on error, add at least 1 dummy opcode
    {
        _emit(CM_DONE, 0);
        ++start;
    }

    if(_skipSpace() && _parseExpr(0) && _skipSpace() && *ptr == 0)
    {
        _emit(CM_DONE, 0);
        top.accept();
        return start;
    }
    return 0;
}

// 1337
// 3.141596
// -42
// -123.456
// .5
// -.5
// 1.
bool Parser::_parseNum(Var& v)
{
    ParserTop top(*this);
    u64 i = 0;
    int parts = 0;

    bool neg = _eat('-');

    if (_parseDecimal(i))
    {
        if (!neg) // it's just unsigned, easy
            v.setUint(mem, i);
        else if (isValidNumericCast<s64>(i)) // but does it fit?
            v.setInt(mem, -s64(i));
        parts |= 1;
    }
    else
        i = 0;

    if(_eat('.'))
    {
        double d;
        if (_addMantissa(d, i))
            parts |= 2;
        v.setFloat(mem, !neg ? d : -d);
    }

    if (!parts)
        v.clear(mem);

    return parts && top.accept();
}

// "hello"
// 'world'
bool Parser::_parseStr(Var& v)
{
    ParserTop top(*this);
    const char open = *ptr++;
    if(!(open == '\'' || open == '\"'))
        return false;

    if (!_parseTextUntil(v, open) && _eat(open))
    {
        v.clear(mem);
        return false;
    }

    return top.accept();
}


// parse until closing char is reached;
// upon successful return, ptr points to the closing char
bool view::Parser::_parseTextUntil(Var& v, char close)
{
    return _parseTextUntilAnyOf(v, &close, 1);
}
bool view::Parser::_parseTextUntilAnyOf(Var& v, const char *close)
{
    return _parseTextUntilAnyOf(v, close, strlen(close));
}
bool view::Parser::_parseTextUntilAnyOf(Var& v, const char *close, size_t n)
{
    const char* s = ptr;
    bool esc = false;
    bool terminated = false;
    for (char c;;)
    {
        c = *s;
        if(!esc)
            for(size_t i = 0; i < n; ++i)
                if (c == close[i])
                {
                    terminated = true;
                    goto done;
                }
        if (!c) // unterminated
            break;
        esc = c == ESC_CHAR;
        ++s;
    }
done:
    v.setStr(mem, ptr, s - ptr);
    ptr = s; // ptr is now on the closing char
    return terminated;
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

bool Parser::_parseSize(size_t& i)
{
    NumConvertResult nr = strtosizeNN(&i, ptr);
    bool ok = nr.ok();
    if (ok)
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
    {
        if (mul_check_overflow<unsigned>(&div, div, 10))
            return false;
    }
    while(--nr.used);

    f = double(i) + (double(m) / double(div));
    ptr = s;
    return true;
}

// There are two things that can be inside of un-quoted text:
// 1a) Just a query: "{/path/to/thing}"
// 1b) Queries among text, like this:
//   "some text {/query/sub/} more text {query2} last text"
// 2) Variable expansion:
//   "n is $n, text is ${text lowercase}"
bool Parser::_parseUnquotedText(char close)
{
    ParserTop top(*this);
    Var text;
    unsigned parts = 0;
    while(_parseTextUntilAnyOf(text, "${"))
    {
        if(text.size())
        {
            _emitPushLiteral(std::move(text));
            ++parts;
        }
        text.clear(mem);

        if (!_parseQuery())
            return false;
    }

    // last part; \0-terminated this time (or whatever our terminator is, in case we're recursively called)
    if (_parseTextUntil(text, close) && text.size())
    {
        _emitPushLiteral(std::move(text));
        ++parts;
    }
    text.clear(mem);

    if (!parts)
        return false;

    if (parts > 1)
        _emit(CM_CONCAT, parts);

    return top.accept();
}

bool Parser::_parseQuery()
{
    ParserTop top(*this);
    return _eat('{') && _parseExpr('}') && _eat('}') && top.accept();
}

// /path/to/thing[...]/blah
// -> any number of keys and selectors
bool Parser::_parseQueryBody()
{
    ParserTop top(*this);
    _skipSpace();
    if(!_parseLookupRoot())
        return false;

    while(_skipSpace() &&_parseLookupNext() && _skipSpace()) {}
    return top.accept();
}

bool Parser::_parseLookupRoot()
{
    return _parseEval() || _parseKey() || _parseSelector();
}

bool Parser::_parseLookupNext()
{
    return _parseKey() || _parseSelector();
}

// /key
// /'key with spaces or /'
bool Parser::_parseKey()
{
    ParserTop top(*this);
    if(!_eat('/'))
        return false;

    Var k;
    if (!_parseStr(k)) // try quoted literal first
        _parseTextUntilAnyOf(k, "/[]{}"); // otherwise parse as far as possible
    if (k.type() != Var::TYPE_STRING)
    {
        k.clear(mem);
        return false;
    }

    _emitGetKey(std::move(k));

    return top.accept(); // zero-length key is okay and is the empty string
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
    bool ok = _parseIdentOrStr(id);
    if (ok)
        _emitPushVarRef(std::move(id));
    return ok;
}

// {expr}
bool Parser::_parseExtendedEval()
{
    ParserTop top(*this);
    Var id;
    if(_eat('{') && _skipSpace() && _parseExpr('}'))
    {
        // all following things are transform names
        while(_skipSpace() && _parseIdentOrStr(id))
        {
            int tr = GetTransformID(id.asCString(mem));
            if (tr < 0)
                return false;
            _emit(CM_TRANSFORM, (unsigned)tr);
        }
    }
    id.clear(mem);
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

// :5,15:20,50,100:
bool Parser::_parseRange(Var& r)
{
    ParserTop top(*this);
    size_t n = 0;
    std::vector<Var::Range> rs;
    while(_skipSpace() && _parseRangeEntry(rs) && _skipSpace())
    {
        _parseVerbatim(","); // last ',' is optional
        _skipSpace();
        ++n;
    }
    if(!n)
        return false;

    r.setRange(mem, rs.data(), rs.size());
    return top.accept();
}

// 2:5
// :5
// 1:
// 5
bool Parser::_parseRangeEntry(std::vector<Var::Range>& rs)
{
    ParserTop top(*this);
    size_t a = 0, b = (size_t)-1;
    size_t n = 0;
    if(_parseSize(a))
        ++n;
    else
        a = 0;
    _skipSpace();
    if(_parseVerbatim(":"))
    {
        _skipSpace();
        if(_parseSize(b))
            ++n;
        else
            b = (size_t)-1;
    }
    else
        b = a + 1;
    _skipSpace();

    if(!n)
        return false;

    Var::Range r { a, b };
    rs.push_back(r);
    return top.accept();
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
// name < 5       -- oprators for key check (spaces optional)
// keep name newname=oldname --
// drop name1 name2 name3
// 10:20,5    -- a range
// only simple ops for now, no precedence, no grouping with braces
bool Parser::_parseSelection()
{
    ParserTop top(*this);
    Var v;
    _skipSpace();

    bool ok = false;
    if(_parseKeyCmp() || _parseKeySel())
    {
        ok = true;
    }
    else if(_parseRange(v))
    {
        ok = true;
        unsigned idx = _addLiteral(std::move(v));
        _emit(CM_SELECT, idx, 0);
    }
    /*else if(_parseEval()) // TODO: finalize exec impl
    {
        ok = true;
        _emit(CM_SELECTSTACK, 0, 0);
    }*/
    else if (_eat('*'))
    {
        _emit(CM_TRANSFORM, GetTransformID("unpack"));
        ok = true;
    }

    return ok &&  _skipSpace() && top.accept();
}

// inside []:
// name=<literal>
// name=$var
// '/name with spaces'=..
bool Parser::_parseKeyCmp()
{
    ParserTop top(*this);
    Var id, lit;
    Cmd op;
    bool ok = false;
    if(_parseIdentOrStr(id) && _skipSpace() && _parseBinOp(op) && _skipSpace())
    {
        ParserTop top2(*this);
        if(_parseLiteral(lit))
        {
            _emitCheckKey(std::move(id), std::move(lit), op.param);
            ok = top2.accept();
        }
        else
        {
            //_emit(CM_DUP, 0);
            //_emitGetKey(std::move(id));
            if (_parseEval())
            {
                unsigned kidx = _addLiteral(std::move(id));
                op.param |= kidx << 4;
                exec.cmds.push_back(op);
                ok = top2.accept();
            }
        }

    }
    id.clear(mem);
    lit.clear(mem);
    return ok && _skipSpace() && top.accept();
}

bool Parser::_parseKeySel()
{
    ParserTop top(*this);

    unsigned keep = 0;
    if(_parseVerbatim("keep"))
        keep = 1;
    else if(!_parseVerbatim("drop"))
        return false;

    if(!_skipSpace(true))
        return false;

    size_t n = 0;
    Var entries;
    Var::Map *m = entries.makeMap(mem);
    while(_parseKeySelEntry(*m, !!keep) && _skipSpace())
        ++n;

    if(!n)
        return false;

    unsigned lit = _addLiteral(std::move(entries));
    _emit(CM_KEYSEL, (lit << 1) | keep, 0);

    return top.accept();
}

bool Parser::_parseKeySelEntry(Var::Map& m, bool allowRename)
{
    ParserTop top(*this);

    Var k, v, *pv = &k;
    if(!_parseIdentOrStr(k))
        return false;

    if(allowRename && _skipSpace() && _parseVerbatim("=") && _skipSpace())
    {
        if(!_parseIdentOrStr(v))
            return false;
        pv = &v;
    }
    _skipSpace();


    StrRef kr = k.asStrRef();
    m.put(mem, kr, std::move(*pv));
    k.clear(mem);
    v.clear(mem);
    return top.accept();
}

bool Parser::_parseBinOp(Cmd& cm)
{
    ParserTop top(*this);
    unsigned invert = _eat('!'); // universal negation -- just stick ! in front of it
    for(size_t i = 0; i < Countof(ops); ++i)
    {
        const char *os = ops[i].text;
        if(size_t n = _parseVerbatim(os))
        {
            cm.type = CM_FILTER;
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
