#pragma once

class DataTree;

void handlesigs(void (*f)(int));
void bail(const char* a, const char* b);
static bool loadcfg(DataTree& base, const char* fn);
bool doargs(DataTree& tree, int argc, char** argv);
