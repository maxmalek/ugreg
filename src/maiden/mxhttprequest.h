#pragma once

#include "variant.h"

enum MxGetJsonResult
{
    MXGJ_OK,
    MXGJ_CONNECT_FAILED,
    MXGJ_PARSE_ERROR
};

MxGetJsonResult mxGetJson(VarRef dst, const char *host, unsigned port, const char *res, int timeoutMS = -1, size_t maxsize = 0);
