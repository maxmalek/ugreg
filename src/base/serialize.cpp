#include "serialize.h"
#include <string.h>
#include <stdio.h>
#include "json_in.h"
#include "json_out.h"
#include "bj.h"

namespace serialize {


Format serialize::formatFromFileName(const char* fn)
{
    const char *lastdot = strrchr(fn, '.');
    if(lastdot)
    {
        const char * const ext = lastdot + 1;
        if(!strcmp(ext, "json"))
            return JSON;
        if(!strcmp(ext, "bj"))
            return BJ;
    }
    return AUTO;
}

struct FILEAutoClose
{
    FILEAutoClose(FILE *fh) : fh(fh) {}
    ~FILEAutoClose()
    {
        if(fh)
            fclose(fh);
    }
    operator FILE* () const { return fh; }
    FILE * const fh;
};

bool load(VarRef dst, const char* fn, Format fmt)
{
    FILEAutoClose fh(fopen(fn, "rb"));
    if(fh)
    {
        char buf[1024 * 8];
        BufferedFILEReadStream rs(fh, buf, sizeof(buf));
        rs.init();
        return load(dst, rs, fmt);
    }

    return false;
}

bool load(VarRef dst, BufferedReadStream& rs, Format fmt)
{
    rs.init(); // just in case

    bool ret = false;
    Var tmp = std::move(*dst.v);

    if(fmt == AUTO)
        if(const char *p = rs.Peek4())
        {
            if(bj::checkMagic4(p))
                fmt = BJ;
            else
                fmt = JSON;
        }
    
    if(fmt == JSON)
        ret = loadJsonDestructive(dst, rs);
    else if(fmt == BJ)
        ret = bj::decode_json(dst, rs);

    if(!ret)
        std::swap(*dst.v, tmp);

    tmp.clear(*dst.mem);
    return ret;
}

bool save(const char* fn, VarCRef src, Format fmt)
{
    return false;
}

bool save(BufferedWriteStream& ws, VarCRef src, Format fmt)
{
    return false;
}


} // end namespace serialize
