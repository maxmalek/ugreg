#pragma once

#include "variant.h"
#include <vector>
#include "treemem.h"

namespace view {

// VarList has pointers for vectorized operation. A CmdType always affects all entries in a list
typedef std::vector<VarCRef> VarRefs;
typedef std::vector<Var> VarStore;

struct StackFrame
{
    VarRefs refs;
    VarStore store; // in case we do a transform, this holds the transformed values

    void clear(TreeMem& mem);
};

typedef void (*TransformFunc)(TreeMem& mem, StackFrame& newframe, const StackFrame& oldframe);

enum CmdType
{
    CM_GETKEY,     // param = index into literals table (to look up name of key). replace top with top[key].
    CM_GETVAR,     // param = index into literals table (to look up variable name). push value of variable.
    CM_TRANSFORM,  // param = function ID. transform top in place.
    CM_COMPARE,    // param = (OpType << 1) | invert. pop A, B; push op(A, B)
    CM_LITERAL,    // param = index intro literals table. push literal on top of the stack
    CM_DUP,        // copy stack frame contents at stack[stack.size() - param - 1] on top as new frame
    CM_CHECKKEY,   // shortcut. key can be a json pointer (if it starts with '/', or just a regular key name)
                   // param = invert | (OpType << 1) | (index << 4); index into literals table (to look up name of key)
                   // param2 = index of the literal to check against in the literals table

    // ALWAYS LAST
    CM_DONE        // terminate execution at this point.
};

unsigned GetTransformID(const char* s);

struct Cmd
{
    CmdType type;
    unsigned param;
    unsigned param2;
};

typedef std::vector<Cmd> Commands;

class Executable
{
public:
    Executable(TreeMem& mem);
    ~Executable();
    size_t disasm(std::vector<std::string>& out) const;

    Commands cmds;
    std::vector<Var> literals;
    TreeMem& mem;
};

class VM : private TreeMem  // each VM has its own memory space to work independently
{
public:
    VM(const Executable& ex, VarCRef constants);
    ~VM();

    void push(VarCRef v);
    bool exec(size_t ip);
    void reset();

    const VarRefs& results() const;

private:

    StackFrame& _topframe();
    StackFrame _popframe();

    // It's a stack machine. This is the stack of lists to process. The top is operated on.
    std::vector<StackFrame> stack;

    Var vars;     // always map (changes while executing)
    Var literals; // always array (constant after init)
    const Commands& cmds;

    void cmd_GetKey(unsigned param);
    void cmd_CheckKey(unsigned param, unsigned lit);
};

} // end namespace view
