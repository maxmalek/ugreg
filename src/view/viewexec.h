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

// A transform must fully write newframe. oldframe will be destroyed after the call,
// and any pointers remaining to its store would cause a segfault.
typedef void (*TransformFunc)(TreeMem& mem, StackFrame& newframe, StackFrame& oldframe);

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

// >= 0 when successful. < 0 when there is no transform with that name
int GetTransformID(const char* s);

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

    bool run(VarCRef v);
    const VarRefs& results() const; // only valid until reset() or re-run

protected:
    void push(VarCRef v);
    bool exec(size_t ip);

    // Clear stack and variables, but not literals
    void reset();

    StackFrame *storeTop(StrRef s); // detach & store
    StackFrame *detachTop(); // move current top to newly allocated frame

private:

    StackFrame& _topframe();
    StackFrame _popframe();

    // recurse into sub-expr of that var and alloc new frame for the result
    StackFrame* _evalVar(StrRef s, size_t pc);

    // look up a frame or eval it if not present
    StackFrame* _getVar(StrRef s);

    // It's a stack machine. This is the stack of lists to process. The top is operated on.
    std::vector<StackFrame> stack;

    VarCRef _base;
    Var vars;     // always map (changes while executing). values are either uint or ptr. ptr is a StackFrame*, uint is the Position to start executing if stackframe wasn't resolved yet
    Var literals; // always array (constant after init)
    const Commands& cmds;

    void cmd_GetKey(unsigned param);
    void cmd_CheckKey(unsigned param, unsigned lit);
    void cmd_PushVar(unsigned param);
    void cmd_Transform(unsigned param);
};

} // end namespace view
