#include <string.h>
#include "variant.h"
#include "bj.h"
#include "jsonstreamwrapper.h"
#include "json_in.h"
#include "json_out.h"
#include "scopetimer.h"


int main(int argc, char **argv)
{
    char buf[8*1024];
    DataTree tre;
    int ret = 0;
    {
        ScopeTimer t;
        BufferedFILEReadStream rd(stdin, buf, sizeof(buf));
        rd.init();
        ret = !bj::decode_json(tre.root(), rd);
    }
    return ret;
}
