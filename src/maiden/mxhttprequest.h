#pragma once

#include "variant.h"
#include "webstuff.h"
#include "request.h"

struct mg_connection;

enum MxGetJsonCode
{
    MXGJ_OK,
    MXGJ_CONNECT_FAILED,
    MXGJ_PARSE_ERROR,
    MXGJ_HTTP_ERROR
};

struct MxGetJsonResult
{
    MxGetJsonCode code;
    int httpstatus;
    std::string errmsg;

    std::string getErrorMsg() const;
};

mg_connection* mxConnectTo(const URLTarget& target, char* errbuf, size_t errbufsz);
MxGetJsonResult mxRequestJson(RequestType rqt, VarRef dst, const URLTarget& target, const VarCRef& data = VarCRef(), const VarCRef& headers = VarCRef(), int timeoutMS = -1, size_t maxsize = 0);
