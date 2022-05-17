#include "mxdefines.h"
#include "util.h"
#include <assert.h>

static const char *mxErrorStrs[] =
{
    "M_OK", // no error
    "M_NOT_FOUND",
    "M_MISSING_PARAMS",
    "M_INVALID_PARAM",
    "M_SESSION_NOT_VALIDATED",
    "M_NO_VALID_SESSION",
    "M_SESSION_EXPIRED",
    "M_INVALID_EMAIL",
    "M_EMAIL_SEND_ERROR",
    "M_INVALID_ADDRESS",
    "M_SEND_ERROR",
    "M_UNRECOGNIZED",
    "M_THREEPID_IN_USE",
    "M_UNKNOWN",

    // if you change this: keep in sync with array in .h file
};

const char* mxErrorStr(MxError err)
{
    assert(err < Countof(mxErrorStrs));
    return err < Countof(mxErrorStrs) ? mxErrorStrs[err] : NULL;
}