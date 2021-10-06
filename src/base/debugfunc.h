#pragma once

#include <sstream>
#include "variant.h"

void varToString(std::ostringstream& os, VarCRef v);
void dumpAllocInfoToString(std::ostringstream& os, const BlockAllocator& mem);
