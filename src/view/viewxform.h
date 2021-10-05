

class TreeMem;

namespace view {

struct StackFrame;

// A transform must fully write newframe. oldframe will be destroyed after the call,
// and any pointers remaining to its store would cause a segfault.
// To keep the store, move it to newframe.
typedef void (*TransformFunc)(TreeMem& mem, StackFrame& newframe, StackFrame& oldframe);


void transformUnpack(TreeMem& mem, StackFrame& newframe, StackFrame& oldframe);
void transformToInt(TreeMem& mem, StackFrame& newframe, StackFrame& oldframe);
void transformCompact(TreeMem& mem, StackFrame& newframe, StackFrame& oldframe);
void transformAsArray(TreeMem& mem, StackFrame& newframe, StackFrame& oldframe);
void transformAsMap(TreeMem& mem, StackFrame& newframe, StackFrame& oldframe);



} // end namespace view
