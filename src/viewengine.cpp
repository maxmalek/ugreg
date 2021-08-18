#include "viewengine.h"
#include <vector>
#include <string>
#include "util.h"
#include "safe_numerics.h"

/*
enum TokenType
{
    TOK_INVALID,
    TOK_END,
    TOK_LSQUARE,
    TOK_RSQUARE,
    TOK_LCURLY,
    TOK_RCURLY,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LANGLE,
    TOK_RANGLE,
    TOK_SINGLEQUOTE,
    TOK_DOUBLEQUOTE,
    TOK_EQ,
    TOK_SLASH,
    TOK_DOLLAR,
    TOK_TEXT,
};

struct Token
{
    TokenType type;
    unsigned line;
    unsigned col;
    unsigned textref;
};

typedef std::vector<Token> TokenList;
*/

enum CommandType
{

};

struct Command
{
    CommandType type;
    std::string text;
};

enum ParserStateType
{
    PARSE_NORMAL,
    PARSE_SELECTOR, // inside [...]
    PARSE_EVAL,     // inside {...}
};

enum TokenType
{
    TOK_OP
};

struct ParserState
{
    size_t stackidx;
    const char *ptr;
};

class Parser
{
public:
    Parser(TreeMem& mem);
    ParserState snapshot() const { return state; }
    void rewind(const ParserState& ps) { state = ps; }
    bool parse(const char *s);

private:

    bool _parseLookup(bool ignoreStartSlash = false);
    bool _parseKey(bool ignoreStartSlash = false);
    bool _parseNum(Var& v);
    bool _parseStr(Var& v);
    bool _parseNull(Var& v);
    bool _parseLiteral();
    bool _parseValue(); // literal or eval
    bool _parseDecimal(u64& i);
    bool _addMantissa(double& f, u64 i);
    bool _parseSelector();
    bool _parseSelection();
    bool _parseEval();
    bool _parseSimpleEval();
    bool _parseExtendedEval();
    bool _parseIdent();
    bool _parseIdentList();
    bool _skipSpace(bool require = false);
    bool _eat(char c);
    bool _isSep() const; // next char is separator, ie. []/,space,etc or end of string
    bool _parseExpr();
    bool _parseOp();

    /*bool _push(ParserStateType t) { statestack.push_back(t); return true; }
    bool _pop(ParserStateType t)
    {
        ParserStateType cur = statestack.back();
        statestack.pop_back();
        return cur == t;
    }*/


    ParserState state;
    TreeMem& mem;
    std::vector<ParserStateType> statestack;

};

static inline bool _IsSep(char c)
{
    return c == '[' || c == ']' || c == '/' || c == ' ' || c == '\t' || !c;
}

bool Parser::_isSep() const
{
    return _IsSep(*state.ptr);
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

bool Parser::parse(const char *s)
{
    //statestack.resize(1);
    //statestack[0] = PARSE_NORMAL;
    state.ptr = s;
    state.stackidx = 0;

    return _parseLookup();
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

bool Parser::_parseLiteral()
{
    Var v;
    return _parseStr(v) || _parseNum(v) || _parseNull(v);
}

bool Parser::_parseValue()
{
    return _parseEval() || _parseLiteral();
}

bool Parser::_parseDecimal(u64& i)
{
    NumConvertResult nr = strtou64NN(&i, state.ptr);
    bool ok = nr.ok();
    if(ok)
        state.ptr += nr.used;
    return ok;
}

bool Parser::_addMantissa(double& f, u64 i)
{
    const char *s = state.ptr;
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
    state.ptr = s;
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
    const char *s = state.ptr;
    if(s[0] != '/')
    {
        if(!ignoreStartSlash)
            return false;
        ++s;
    }
    
    for(char c; (c = *s) && c != '/' && c != '['; ++s)
    {
    }

    return true; // zero-length key is okay and is the empty string
}

// [expr]
bool Parser::_parseSelector()
{
    ParserTop top(*this);
    return _eat('[') && _parseSelection() && _eat(']') && top.accept();
}

// $ident
// ${lookup}
bool Parser::_parseEval()
{
    ParserTop top(*this);
    return _eat('$') && (_parseExtendedEval() || _parseSimpleEval()) && top.accept();
}

// lookup ident
bool Parser::_parseSimpleEval()
{
    return _parseIdent();
}

// {ident followed by modifiers}
bool Parser::_parseExtendedEval()
{
    ParserTop top(*this);
    return _eat('{') && _skipSpace() && _parseLookup() && _skipSpace() && _eat('}') && top.accept();
}

bool Parser::_parseIdent()
{
    const char *s = state.ptr;
    char c;
    while((c = *s) && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'))
        ++s;

    bool ok = s != state.ptr;
    if(ok)
        state.ptr = s + 1;
    return ok;
}

bool Parser::_parseIdentList()
{
    ParserTop top(*this);
    _skipSpace();
    size_t n = 0;
    for(;;)
    {
        if(_parseIdent())
        {
            ++n;
            _skipSpace();
        }
        else
            break;
    }
    return n && top.accept();
}

bool Parser::_skipSpace(bool require)
{
    const char *s = state.ptr;
    char c;
    while((c = *s++) && (c == ' ' || c == '\t')) {}
    bool skipped = s != state.ptr;
    state.ptr = s;
    return !require || skipped;
}

bool Parser::_eat(char c)
{
    bool eq = c == *state.ptr;
    state.ptr += eq;
    return eq;
}

// anything inside [], can be:
// name=value
// /path/to/subey < value
// only simple ops for now, no precedence, no grouping with braces
bool Parser::_parseSelection()
{
    ParserTop top(*this);
    return _parseLookup(false) && _skipSpace() && _parseOp() && _skipSpace() && _parseValue() && top.accept();
}

// primitive comparisons
enum OpType
{
    OP_EQ,
    OP_LT,
    OP_GT,
};

enum { MAX_OP_LEN = 2 };
struct OpEntry
{
    const char text[MAX_OP_LEN + 1]; // + \0
    OpType op;
    int invert;
};

static const OpEntry ops[] =
{
    { "=",  OP_EQ, 0},
    { "!=", OP_EQ, 1 },
    { "<",  OP_LT, 0 },
    { ">=", OP_LT, 1 },
    { ">",  OP_GT, 0 },
    { "<=", OP_GT, 1 },
};

bool Parser::_parseOp()
{
    const char * const s = state.ptr;
    for(size_t i = 0; i < Countof(ops); ++i)
    {
        const char *os = ops[i].text;
        char c;
        for(unsigned k = 0 ;; ++k)
        {
            char c = *os++;
            if(!c) // got it!
            {
                // TODO: emit
                state.ptr = s + k - 1;
                return true;
            }
            if(s[k] != c)
                break; // it's not this op, check the next
        }
    }
    return false;
}
