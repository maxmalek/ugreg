#pragma once

#include "types.h"

class TreeMem;

namespace view {

struct StackFrame;

// A function must fully write newframe. All paramFrames will be destroyed after the call,
// and any pointers remaining to its store would cause a segfault.
// To keep the store, move it to newframe.
typedef const char * (*ViewFunc)(TreeMem& mem, StackFrame& newframe, StackFrame *paramFrames, size_t nparams);


const char *transformUnpack (TreeMem& mem, StackFrame& newframe, StackFrame *paramFrames, size_t nparams);
const char *transformToInt  (TreeMem& mem, StackFrame& newframe, StackFrame *paramFrames, size_t nparams);
const char *transformCompact(TreeMem& mem, StackFrame& newframe, StackFrame *paramFrames, size_t nparams);
const char *transformAsArray(TreeMem& mem, StackFrame& newframe, StackFrame *paramFrames, size_t nparams);
const char *transformAsMap  (TreeMem& mem, StackFrame& newframe, StackFrame *paramFrames, size_t nparams);
const char *transformToKeys (TreeMem& mem, StackFrame& newframe, StackFrame *paramFrames, size_t nparams);



} // end namespace view
