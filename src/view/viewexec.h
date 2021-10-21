#pragma once

#include "variant.h"
#include <vector>
#include <string>
#include "treemem.h"

namespace view {

// For the VM stack contents, keep each value together with its key (in case the value came from a map)
struct VarEntry
{
    VarCRef ref;
    StrRef key;
    const Var::Extra *extra; // last seen extra data under which ref is located
};

// VarList has pointers for vectorized operation. A CmdType always affects all entries in a list

typedef std::vector<VarEntry> VarRefs;
typedef std::vector<Var> VarStore;


// Invariants:
// - StackFrame.refs may reference any store in its own frame or a StackFrame below it
// - refs may come from anywhere, except higher stackframes
// - a frame can have refs to many different memory spaces, but anything in store has the same memory space as the VM.
struct StackFrame
{
    VarRefs refs;
    VarStore store; // in case we do a transform, this holds the transformed values

    void clear(TreeMem& mem);

    // because pushing to frame.store may realloc the vector, either reserve frame.store
    // with the correct size beforehand and then use addAbs(),
    // or use addRel() to add stuff and call makeAbs() when done
    void addRel(TreeMem& mem, Var&& v, StrRef k);
    void addAbs(TreeMem& mem, Var&& v, StrRef k);
    void makeAbs();
};

struct EntryPoint
{
    std::string name;
    size_t idx;
};

enum CmdType
{
    CM_GETKEY,     // param = index into literals table (to look up name of key). replace top with top[key].
    CM_GETVAR,     // param = index into literals table (to look up variable name). push value of variable.
    CM_TRANSFORM,  // param = function ID. transform top in place.
    CM_FILTER,     // param = (OpType << 1) | invert. pop A, keep elements in top only when op(top, A) is true
    CM_LITERAL,    // param = index intro literals table. push literal on top of the stack
    CM_DUP,        // copy stack frame contents at stack[stack.size() - param - 1] on top as new frame
    CM_CHECKKEY,   // shortcut. key can be a json pointer (if it starts with '/', or just a regular key name)
                   // param = invert | (OpType << 1) | (index << 4); index into literals table (to look up name of key)
                   // param2 = index of the literal to check against in the literals table
    CM_KEYSEL,     // param = (keep | (index << 1)); index into literals table. keep = 1 keeps the keys, 0 drops them
    CM_SELECT,     // param = index into literals table
    CM_SELECTSTACK,// no param
    CM_CONCAT,     // param = how many stack frames to concat

    // ALWAYS LAST
    CM_DONE        // terminate execution at this point.
};

// >= 0 when successful. < 0 when there is no transform with that name
int GetTransformID(const char* s);
const char *GetTransformName(int id);

struct Cmd
{
    CmdType type;
    unsigned param;
    unsigned param2;
};

typedef std::vector<Cmd> Commands;

// An executable contains the bytecode to be run by the VM
class Executable
{
public:
    Executable(TreeMem& mem);
    Executable(Executable&& o) noexcept;
    ~Executable();
    size_t disasm(std::vector<std::string>& out) const;
    void clear();

    Commands cmds;
    std::vector<Var> literals;
    TreeMem *mem;
};

class VM : public TreeMem  // each VM has its own memory space to work independently
{
public:
    VM();
    ~VM();
    void init(const Executable& ex, const EntryPoint* eps, size_t numep);
    VarRef makeVar(const char *name, size_t len); // caller must fill returned ref

    bool run(VarCRef v); // pass root of tree to operate on
    const VarRefs& results() const; // only valid until reset() or re-run

protected:
    void push(VarCRef v);
    bool exec(size_t ip);

    // Clear stack and variables, but not literals or evals
    void reset();

    StackFrame *storeTop(StrRef s); // detach & store
    StackFrame *detachTop(); // move current top to newly allocated frame

private:
    void _freeStackFrame(void* p);

    StackFrame& _topframe();
    StackFrame _popframe();

    // recurse into sub-expr of that var and alloc new frame for the result
    StackFrame* _evalVar(StrRef s, size_t pc);

    // look up a frame or eval it if not present
    StackFrame* _getVar(StrRef s);

    // It's a stack machine. This is the stack of lists to process. The top is operated on.
    std::vector<StackFrame> stack;

    VarCRef _base;
    Var evals;     // always map (changes while executing). values are either uint or ptr. ptr is a StackFrame*, uint is the Position to start executing if stackframe wasn't resolved yet
    Var literals; // always array (constant after init)
    Commands cmds; // copied(!) from Executable

    void cmd_GetKey(unsigned param);
    void cmd_CheckKeyVsSingleLiteral(unsigned param, unsigned lit);
    void cmd_PushVar(unsigned param);
    void cmd_Transform(unsigned param);
    //void cmd_Compare(unsigned param);
    void cmd_Filter(unsigned param);
    void cmd_Keysel(unsigned param);
    void cmd_Select(unsigned param);
    void cmd_SelectStack();
};

} // end namespace view
