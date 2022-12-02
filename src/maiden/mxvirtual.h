#pragma once

#include "variant.h"

struct EvTreeRebuilt
{
    virtual void onTreeRebuilt(VarCRef src) = 0;
};
