#pragma once

#include "variant.h"
#include "webstuff.h"
#include "request.h"

struct mg_connection;

enum MxGetJsonResult
{
    MXGJ_OK,
    MXGJ_CONNECT_FAILED,
    MXGJ_PARSE_ERROR
};

mg_connection* mxConnectTo(const URLTarget& target);
MxGetJsonResult mxRequestJson(RequestType rqt, VarRef dst, const URLTarget& target, const VarCRef& data = VarCRef(), int timeoutMS = -1, size_t maxsize = 0);
