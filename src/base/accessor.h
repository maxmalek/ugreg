#pragma once

#include <vector>
#include <utility>
#include "variant.h"
#include "treemem.h"

// Stores access information required to access restricted _VarMap.
// Mostly used in conjunction with _VarExtra.
class Accessor
{
public:
    Accessor();
    ~Accessor();
    const u64 ts;
    // TODO: access rights
};
