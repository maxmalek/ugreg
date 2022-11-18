#include "serialize.h"
#include <string.h>
#include <stdio.h>
#include "json_in.h"
#include "json_out.h"
#include "bj.h"
#include "zstdstream.h"

namespace serialize {


// should be filename.format[.compr]
// but this is actually rather lenient
static void deduceFromFileName(Format *fmt, Compression *cmp, const char* const fn)
{
    const char *pos = fn;
    for(size_t i = 0; i < 2; ++i)
    {
        const char *lastdot = strrchr(pos, '.');
        if(lastdot)
        {
            const char * const ext = lastdot + 1;
            if(fmt && *fmt == AUTO)
            {
                if(!strncmp(ext, "json", 4))
                    *fmt = JSON;
                else if(!strncmp(ext, "bj", 2))
                    *fmt = BJ;
            }
            if(cmp && *cmp == AUTOC)
            {
                if(!strncmp(ext, "zst", 3))
                    *cmp = ZSTD;
            }

            pos = lastdot - 1;
            if(pos < fn)
                break;
        }

    }
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

static bool loadZstd(VarRef dst, BufferedReadStream& rs, Format fmt)
{
    char buf[1024 * 8];
    ZstdReadStream zs(rs, buf, sizeof(buf));
    zs.init();
    return load(dst, zs, fmt);
}

bool load(VarRef dst, const char* fn, Compression comp, Format fmt)
{
    FILEAutoClose fh(fopen(fn, "rb"));
    if(!fh)
        return false;

    char buf[1024 * 8];
    BufferedFILEReadStream rs(fh, buf, sizeof(buf));
    rs.init();

    deduceFromFileName(NULL, &comp, fn); // format detection is based on the actual data, not the file name

    switch(comp)
    {
        case RAW: return load(dst, rs, fmt);
        case ZSTD: return loadZstd(dst, rs, fmt);
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

bool save(BufferedWriteStream& ws, VarCRef src, Format fmt)
{
    ws.init();

    switch(fmt)
    {
        case AUTO:
        case JSON:
            writeJson(ws, src, false);
            return !ws.isError();

        case BJ:
        {
            BlockAllocator alloc;
            return bj::encode(ws, src, &alloc) && !ws.isError();
        }
    }

    return false;
}

static bool saveZstd(BufferedWriteStream& ws, VarCRef src, Format fmt, int level)
{
    char buf[8*1024];
    ZstdWriteStream zs(ws, level, buf, sizeof(buf));
    zs.init();
    return save(zs, src, fmt);
}

bool save(const char* fn, VarCRef src, Compression comp, Format fmt, int level)
{
    FILEAutoClose fh(fopen(fn, "wb"));
    if(!fh)
        return false;

    char buf[8*1024];
    BufferedFILEWriteStream fs(fh, buf, sizeof(buf));
    fs.init();

    deduceFromFileName(&fmt, &comp, fn);

    switch(comp)
    {
        case RAW: return save(fs, src, fmt);
        case ZSTD: return saveZstd(fs, src, fmt, level);
    }

    return false;
}


} // end namespace serialize
