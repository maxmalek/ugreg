#pragma once

#include "variant.h"

int mxGetJson(VarRef dst, const char *host, unsigned port, const char *res, int timeoutMS = -1, size_t maxsize = 0);
