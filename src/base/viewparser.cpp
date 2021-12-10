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

struct KeySelOpEntry
{
    const char text[8];
    KeySelOp op;
};

static const KeySelOpEntry keyselOps[] =
{
    { "keep", KEYSEL_KEEP },
    { "drop", KEYSEL_DROP },
    { "key",  KEYSEL_KEY  },
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

    bool _parseUnquotedText();
    bool _parseEvalRoot();

    // --- main language---
    bool _parseExpr();
    bool _parseEval();
    bool _parseSubExpr(const char* s); // recursive call into self
    bool _parseDot();
    bool _parseTilde();
    // --- variables and identifiers ---
    bool _parseAndEmitVarRef();
    bool _parseVarRef(Var& id);
    bool _parseIdent(Var& id); // write identifier name to id (as string)
    bool _parseIdentOrStr(Var& id);
    // --- function call and transform ---
    bool _parseFnCall(unsigned extraargs = 0);
    unsigned _parseExprList(); // returns number of exprs, 0 on error
    // --- modifiers ---
    bool _parseModList();
    bool _parseMod();
    bool _parseAndEmitTransform();
    bool _parseAndEmitLookup();
    // --- query / tree lookup ---
    bool _parseQuery();
    /*bool _parseQueryBody();
    bool _parseLookupRoot();
    bool _parseLookupNext();
    bool _parseKey();*/
    // --- selection and filtering ---
    bool _parseSelector();
    bool _parseSelection();
    bool _parseKeyCmp();
    bool _parseKeySel();
    bool _parseKeySelOp(KeySelOp& op);
    bool _parseKeySelEntry(Var::Map& m, bool allowRename);
    bool _parseBinOp(Cmd& op);
    // --- terminals (values, literals, etc) ---
    bool _parseAndEmitLiteral();
    bool _parseLiteral(Var& v);
    bool _parseNum(Var& v);
    bool _parseStr(Var& v);
    bool _parseNull();
    bool _parseBool(Var& v);
    bool _parseDecimal(u64& i);
    bool _parseSize(size_t& i);
    size_t _parseVerbatim(const char* in); // returns length of match if matched, otherwise 0
    bool _parseRange(Var& r);
    bool _parseRangeEntry(std::vector<Var::Range>& rs);
    // --- utility ---
    bool _addMantissa(double& f, u64 i);
    bool _parseTextUntil(Var& v, char close);
    bool _parseTextUntilAnyOf(Var& v, const char* close);
    bool _parseTextUntilAnyOf(Var& v, const char* close, size_t n);
    bool _skipSpace(bool require = false);
    bool _eat(char c);

    size_t _emit(CmdType cm, unsigned param, unsigned param2 = 0); // returns index of the emitted instruction
    unsigned _addLiteral(Var&& lit); // return index into literals table
    unsigned _emitPushVarRef(Var&& v);
    unsigned _emitPushLiteral(Var&& v);
    unsigned _emitGetKey(Var&& v);
    unsigned _emitTransform(Var&& id);
    void _emitCheckKey(Var&& key, Var&& lit, unsigned opparam);

    const char *ptr;
    const char *maxptr; // for error reporting only
    TreeMem& mem;
    Executable& exec;
    std::vector<std::string> errors;
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

        if (!p.errors.empty())
        {
            error += "\nExtra errors reported by parser:\n";
            for (size_t i = 0; i < p.errors.size(); ++i)
            {
                error += p.errors[i];
                error += '\n';
            }
        }
    }
    return res;
}

const char* getKeySelOpName(KeySelOp op)
{
    for(size_t i = 0; i < Countof(keyselOps); ++i)
        if(op == keyselOps[i].op)
            return keyselOps[i].text;
    return NULL;
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

// main entry point for parsing a thing
size_t Parser::parse(const char *s)
{
    ptr = s; // this must be done before creating top
    ParserTop top(*this);

    size_t start = exec.cmds.size();
    // since we return 0 only on error, add at least 1 dummy opcode
    // this has the nice side effect that we can use index 0 as invalid,
    // and when it's used anyway/errorneously, it'll just return without doing anything.
    if(!start)
    {
        _emit(CM_DONE, 0);
        ++start;
    }

    if(_parseUnquotedText() && *ptr == 0)
    {
        _emit(CM_DONE, 0);
        top.accept();
        return start;
    }
    return 0;
}

// tokenizes a string into sub-expressions and emits CM_CONCAT if applicable.
// otherwise emits a normal literal string.
// s must be 0-terminated
bool Parser::_parseSubExpr(const char* s)
{
    const char* const oldptr = ptr, * const oldmax = maxptr;
    ptr = s;
    bool ok = _parseUnquotedText();
    ptr = oldptr;
    maxptr = oldmax;
    return ok;
}

bool Parser::_parseDot()
{
    bool ok = _eat('.') && _skipSpace();
    if(ok)
        _emit(CM_DUP, 0); // FIXME: dup last proper top
    return ok;
}

bool Parser::_parseTilde()
{
    bool ok = _eat('~') && _skipSpace();
    if (ok)
        _emit(CM_PUSHROOT, 0); // FIXME: dup last proper top
    return ok;
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

    if (!(_parseTextUntil(v, open) && _eat(open)))
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
    std::ostringstream os;
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
        if(!esc) // ignore escapes
            os << c;
        esc = c == ESC_CHAR;
        ++s;
    }
done:
    v.setStr(mem, os.str().c_str());
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

bool Parser::_parseAndEmitLiteral()
{
    Var lit;
    bool ok = false;
    if(_parseLiteral(lit))
    {
        if(lit.type() == Var::TYPE_STRING)
            ok =  _parseSubExpr(lit.asCString(mem));
        else
        {
            _emitPushLiteral(std::move(lit));
            ok = true;
        }
    }
    lit.clear(mem);
    return ok;
}

bool Parser::_parseAndEmitVarRef()
{
    Var id;
    bool ok = _parseVarRef(id);
    if(ok)
        _emitPushVarRef(std::move(id));
    id.clear(mem);
    return ok;
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

// Does variable expansion:
//   "n is $n, text is ${text lowercase}"
bool Parser::_parseUnquotedText()
{
    ParserTop top(*this);
    Var text;
    unsigned parts = 0;
    while(*ptr && _parseTextUntilAnyOf(text, "$\0", 2))
    {
        if(text.size())
        {
            _emitPushLiteral(std::move(text));
            ++parts;
        }
        text.clear(mem);
        if (!*ptr)
            break;

        if (_parseEvalRoot())
            ++parts;
        else
            return false;
    }

    text.clear(mem);

    if (!parts)
        return false;

    if (parts > 1)
        _emit(CM_CONCAT, parts);

    return top.accept();
}

// $var
// $func( ... )
// ${ expr }
bool Parser::_parseEvalRoot()
{
    ParserTop top(*this);
    bool ok = false;
    if (*ptr == '$')
    {
        {
            ParserTop top2(*this);
            ok = _eat('$') && (
                _parseFnCall() || (_eat('{') && _parseExpr() && _eat('}'))
            ) && top2.accept();
        }

        if (!ok)
        {
            ok = _parseAndEmitVarRef(); // eats a $ on its own
            if(ok && *ptr == '(') // eh? function call?
            {
                ok = false;
                errors.push_back("Opening bracket '(' after variable eval in unquoted text mode, this probably means you meant to do a function call like $func(...) but the param list failed to parse");
            }
        }
    }

    return ok && top.accept();
}

bool Parser::_parseExpr()
{
    return _parseEval() && _parseModList();
}

// any literal value
// $ident
// f( ... )
// { ... }
bool Parser::_parseEval()
{
    return _parseDot() || _parseTilde() || _parseAndEmitLiteral() || _parseFnCall() || _parseAndEmitVarRef() || _parseQuery();
}

bool Parser::_parseQuery()
{
    //ParserTop top(*this);
    //return _eat('{') && _parseQueryBody() && _eat('}') && top.accept();
    return false; // FIXME:
}

/*
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
    if(_parseEval()) // pushes stuff on the stack
        return true;

    // key lookup and selector don't push anything -> push the root
    ParserTop top(*this);
    _emit(CM_PUSHROOT, 0);
    return (_parseKey() || _parseSelector()) && top.accept();
}

bool Parser::_parseLookupNext()
{
    return _parseKey() || _parseSelector();
}
*/

// func(...)
// !! no space before opening bracket and behind closing bracket
bool Parser::_parseFnCall(unsigned extraargs)
{
    ParserTop top(*this);
    Var id;
    bool ok = false;
    if (_parseIdent(id) && _skipSpace() && _eat('(') && _skipSpace())
    {
        // push params on the stack first, then do the call
        if (unsigned n = _parseExprList())
        {
            if (_skipSpace() && _eat(')'))
            {
                // store the literal ID instead of some function ID.
                // this means functions are called by name, which is a tad slower than by some index,
                // but makes adding user-defined functions later on much easier.
                unsigned lit = _addLiteral(std::move(id));
                _emit(CM_CALLFN, n + extraargs, lit);
                ok = true;
            }
        }
    }
    id.clear(mem);
    return ok && top.accept();
}

unsigned Parser::_parseExprList()
{
    ParserTop top(*this);
    unsigned n = 0;
    if (_skipSpace() && _parseExpr())
    {
        ++n;
        while (_skipSpace() && _eat(',') && _skipSpace())
            if (_parseExpr())
                ++n;
            else
                return 0;
    }
    top.accept();
    return n;
}

bool Parser::_parseModList()
{
    while (_skipSpace() && _parseMod())
    {
    }
    return _skipSpace();
}

bool Parser::_parseMod()
{
    return _parseSelector() /*|| _parseQuery()*/ || _parseAndEmitTransform() || _parseAndEmitLookup();
}

// eval | func     // <-- this is effectively f(eval)
// eval | f(42)    // <-- this is effectively f(eval, 42)
bool Parser::_parseAndEmitTransform()
{
    ParserTop top(*this);

    bool ok = false;
    if (_eat('|') && _skipSpace())
    {
        ok = _parseFnCall(1);
        if (!ok)
        {
            Var id;
            ok = _parseIdentOrStr(id);
            if(ok)
                _emitTransform(std::move(id));
        }
    }
    return ok && _skipSpace() && top.accept();
}

bool Parser::_parseAndEmitLookup()
{
    ParserTop top(*this);
    Var id;
    bool ok = _eat('/') && _skipSpace() && _parseIdentOrStr(id);
    if(ok)
        _emitGetKey(std::move(id));
    return ok && _skipSpace() && top.accept();
}

// /key
// /'key with spaces or /'
/*
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
*/

// [ ... ]
bool Parser::_parseSelector()
{
    ParserTop top(*this);
    return _eat('[') && _parseSelection() && _eat(']') && top.accept();
}


/*
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
    if(!_eat('{') && _skipSpace())
        return false;
    Var id;
    bool ok = false;
    if(_parseSimpleEval() || _parseExpr())
    {
        ok = true;
        // all following things are transform names
        while(_skipSpace() && _parseIdentOrStr(id))
        {
            const char* tf = id.asCString(mem);
            int tr = GetTransformID(tf);
            if (tr < 0)
            {
                errors.push_back(std::string("Unknown transform: ") + tf);
                ok = false;
                break;
            }
            _emit(CM_TRANSFORM, (unsigned)tr);
        }
    }
    id.clear(mem);
    return ok && _skipSpace() && _eat('}') && top.accept();
}
*/

bool Parser::_parseVarRef(Var& id)
{
    return _eat('$') && _parseIdentOrStr(id);
}

bool Parser::_parseIdent(Var& id)
{
    const char *s = ptr;
    char c = *s;

    while((c = *s) && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (c >= '0' && c <= '9')))
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
    bool ok = _parseIdent(id) || (_parseLiteral(id) && id.type() == Var::TYPE_STRING);
    if(!ok)
        id.clear(mem);
    return ok;
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
        Var unp(mem, "unpack");
        _emitTransform(std::move(unp));
        ok = true;
    }
    else
    {
        ParserTop top2(*this);
        _emit(CM_DUP, 0);
        ok = _parseExpr() && top2.accept(); // FIXME check this
    }

    return ok && _skipSpace() && top.accept();
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
            // fast check against single literal
            _emitCheckKey(std::move(id), std::move(lit), op.param);
            ok = top2.accept();
        }
        else
        {
            //_emit(CM_DUP, 0);
            //_emitGetKey(std::move(id));
            if (_parseExpr())
            {
                unsigned kidx = _addLiteral(std::move(id));
                op.param |= kidx << 4;
                exec.cmds.push_back(op);
                ok = top2.accept();
            }
            _emit(CM_POP, 0);
        }

    }
    id.clear(mem);
    lit.clear(mem);
    return ok && _skipSpace() && top.accept();
}

// [keep a=b c=d f g]
// [drop a b c]
bool Parser::_parseKeySel()
{
    ParserTop top(*this);

    KeySelOp op;
    if(!_parseKeySelOp(op))
        return false;

    if(!_skipSpace(true))
        return false;

    size_t n = 0;
    Var entries;
    Var::Map *m = entries.makeMap(mem);
    while(_parseKeySelEntry(*m, op == KEYSEL_KEEP) && _skipSpace())
        ++n;

    if(!n)
        return false;

    unsigned lit = _addLiteral(std::move(entries));
    _emit(CM_KEYSEL, (lit << 2) | op, 0);

    return top.accept();
}

bool Parser::_parseKeySelOp(KeySelOp& op)
{
    for(size_t i = 0; i < Countof(keyselOps); ++i)
        if(_parseVerbatim(keyselOps[i].text))
        {
            op = keyselOps[i].op;
            return true;
        }
    return false;
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

size_t Parser::_emit(CmdType cm, unsigned param, unsigned param2)
{
    Cmd c { cm, param, param2 };
    size_t idx = exec.cmds.size();
    exec.cmds.push_back(c);
    return idx;
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
    _emit(CM_LOOKUP, lit);
    return lit;
}

unsigned Parser::_emitTransform(Var&& id)
{
    assert(id.type() == Var::TYPE_STRING);
    unsigned lit = _addLiteral(std::move(id));
    _emit(CM_CALLFN, 1, (unsigned)lit);
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
