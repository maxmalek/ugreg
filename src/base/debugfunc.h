#pragma once

#include <sstream>
#include "variant.h"

bool varToStringDebug(std::ostringstream& os, VarCRef v);
bool varToString(std::ostringstream& os, VarCRef v);
void dumpAllocInfoToString(std::ostringstream& os, const BlockAllocator& mem);
