#pragma once

#include "types.h"

class DataTree;

void handlesigs(void (*f)(int));
[[noreturn]] void bail(const char* a, const char* b);
static bool loadcfg(DataTree& base, const char* fn);

// Called whenever cmd[idx] starts with '-'.
// Return number of consumed args INCLUDING the current one.
// -> Return 0 to indicate error.
typedef size_t (*ArgsCallback)(char **argv, size_t idx, void *ud);

bool doargs(DataTree& tree, size_t argc, char** argv, ArgsCallback cb = 0, void *ud = 0);
