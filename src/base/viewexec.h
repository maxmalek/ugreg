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
    CM_LOOKUP,     // param = index into literals table (to look up name of key). replace top with top[key].
    CM_GETVAR,     // param = index into literals table (to look up variable name). push value of variable.
    CM_FILTERKEY,  // param = (OpType << 1) | invert. pop A, keep elements in top only when op(top, A) is true
    CM_LITERAL,    // param = index intro literals table. push literal on top of the stack
    CM_DUP,        // copy stack frame contents at stack[stack.size() - param - 1] on top as new frame
    CM_CHECKKEY,   // shortcut. key can be a json pointer (if it starts with '/', or just a regular key name)
                   // param = invert | (OpType << 1) | (index << 4); index into literals table (to look up name of key)
                   // param2 = index of the literal to check against in the literals table
    CM_KEYSEL,     // param = (KeySelOp | (index << 2)); index into literals table
    CM_SELECTLIT,     // param = index into literals table
    CM_CONCAT,     // param = how many stack frames to concat
    CM_PUSHROOT,   // no param
    CM_CALLFN,     // param = # of args passed to function, param2 = index to literals table (function name)
    CM_POP,        // pop one stack frame
    CM_SELECTV,    // use values from stack top as keys to select new values frop top-1. pops top.

    // ALWAYS LAST
    CM_DONE        // terminate execution at this point.
};

// currently occupies 2 bits in CM_KEYSEL. code changes are required for more bits.
enum KeySelOp
{
    KEYSEL_KEEP, // keep (and optionally rename) keys in list
    KEYSEL_DROP, // drop keys in list
    KEYSEL_KEY   // if array, convert to map. lookup subvalue from each entry, use that subvalue as new key for entry
};

/* A selection operation [...] can pull its data from:
- one or more objects on top of the stack ( $x[...] )
- the stack top itself (as if it was an object, expr | [...] )
So in case of a suitable selection op this is set to either object or stack.
*/
enum ValueSel // bitmask
{
    SEL_UNSPECIFIED = 0x00,

    // one of both
    SEL_SRC_OBJECT = 0x01, // fast
    SEL_SRC_STACK  = 0x02,  // fast

    // may be used with SEL_SRC_OBJECT
    SEL_DST_REPACK = 0x04, // optional flag, slow!
};

struct Cmd
{
    CmdType type;
    unsigned param;
    unsigned param2;
    ValueSel sel;
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

class VM
{
public:
    VM(TreeMem& mem); // each VM has its own memory space to be able to work independently
    ~VM();
    void init(const Executable& ex, const EntryPoint* eps, size_t numep);
    VarRef makeVar(const char *name, size_t len); // caller must fill returned ref

    bool run(VarCRef v, size_t start = 1); // pass root of tree to operate on
    const VarRefs& results() const; // only valid until reset() or re-run

    TreeMem& mem;

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
    void _popframes(size_t n);

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

    void filterObjects(ValueSel sel, Var::CompareMode cmp, const VarEntry* values, size_t numvalues, const char* keystr, unsigned invert);

    // NULL returns is good, otherwise it's an error message
    const char *cmd_Lookup(unsigned param);
    const char *cmd_CheckKeyVsSingleLiteral(unsigned param, unsigned lit, ValueSel sel);
    const char *cmd_PushVar(unsigned param);
    const char *cmd_FilterKey(unsigned param, ValueSel sel);
    const char *cmd_Keysel(unsigned param);
    const char *cmd_Select(unsigned param);
    const char* cmd_Selectv(unsigned param);
    const char *cmd_Concat(unsigned count);
    const char *cmd_CallFn(unsigned lit, unsigned params);
};

} // end namespace view
