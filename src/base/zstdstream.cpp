#include <string.h>
#include <assert.h>
#include "zstdstream.h"
#include <zstd.h>

ZstdWriteStream::ZstdWriteStream(BufferedWriteStream& sm, int level, char* buf, size_t bufsize)
    : BufferedWriteStream(NULL, _Write, buf, bufsize)
    , _sm(sm)
    , zc(ZSTD_createCStream())
    , _level(level)
{
    ZSTD_CCtx_setParameter((ZSTD_CStream*)zc, ZSTD_c_compressionLevel, level);
    sm.init();
}

ZstdWriteStream::~ZstdWriteStream()
{
    finish();
    ZSTD_freeCStream((ZSTD_CStream*)zc);
}

void ZstdWriteStream::finish()
{
    if(_dst && Tell()) // Only output something if we ever got any data
    {
        Flush();

        ZSTD_inBuffer ib = { NULL, 0, 0 };
        for(;;)
        {
            const BufInfo buf = _sm.getBuffer();
            ZSTD_outBuffer ob = { buf.buf, buf.remain, 0 };
            size_t avail_out = buf.remain;
            u8 *next_out = (u8*)buf.buf;
            size_t res = ZSTD_compressStream2((ZSTD_CStream*)zc, &ob, &ib, ZSTD_e_end);
            if(ZSTD_isError(res))
            {
                assert(false);
                break;
            }
            else if(ob.pos)
                _sm.advanceBuffer(ob.pos);

            if(!res)
                break;
        }

        _sm.Flush();
        _dst = NULL; // make sure the BufferedWriteStream dtor doesn't flush
    }
}

size_t ZstdWriteStream::_Write(const void* src, size_t bytes, BufferedWriteStream* self)
{
    ZstdWriteStream* me = static_cast<ZstdWriteStream*>(self);

    ZSTD_inBuffer ib = { src, bytes, 0 };

    while(ib.size)
    {
        const BufInfo buf = me->_sm.getBuffer();
        ZSTD_outBuffer ob = { buf.buf, buf.remain, 0 };
        size_t avail_out = buf.remain;
        u8 *next_out = (u8*)buf.buf;
        size_t res = ZSTD_compressStream2((ZSTD_CStream*)me->zc, &ob, &ib, ZSTD_e_continue);
        if(ZSTD_isError(res))
        {
            assert(false);
            return 0;
        }
        else if(ob.pos)
            me->_sm.advanceBuffer(ob.pos);
    }
    return bytes;
}
