#pragma once

#include "variant.h"

namespace view {

class Executable;

// Return entry point (index) when successfuly parsed, 0 on error
size_t parse(Executable& exe, const char *s);

}
