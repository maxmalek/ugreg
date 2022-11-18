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
    if(!bytes)
        return 0;

    ZstdWriteStream* me = static_cast<ZstdWriteStream*>(self);

    ZSTD_inBuffer ib = { src, bytes, 0 };

    while(ib.pos < ib.size)
    {
        const BufInfo buf = me->_sm.getBuffer();
        ZSTD_outBuffer ob = { buf.buf, buf.remain, 0 };
        size_t res = ZSTD_compressStream2((ZSTD_CStream*)me->zc, &ob, &ib, ZSTD_e_continue);
        me->_sm.advanceBuffer(ob.pos);
        if(ZSTD_isError(res))
        {
            assert(false);
            break;
        }
    }
    return ib.pos; // how many bytes were consumed
}


ZstdReadStream::ZstdReadStream(BufferedReadStream & src, char * buf, size_t bufsize)
    : BufferedReadStream(NULL, _Read, buf, bufsize)
    , zd(ZSTD_createDStream())
    , _src(src)
{

}

ZstdReadStream::~ZstdReadStream()
{
    ZSTD_freeDStream((ZSTD_DStream*)zd);
}

size_t ZstdReadStream::_Read(void * dst, size_t bytes, BufferedReadStream * self)
{
    ZstdReadStream *my = static_cast<ZstdReadStream*>(self);
    BufferedReadStream& src = my->_src;
    ZSTD_outBuffer ob = { dst, bytes, 0 };

    src.advanceBuffered(0); // make sure the buffer starts in a state that has data

    for(;;)
    {
        ZSTD_inBuffer ib = { src.ptr(), src.availBuffered(), 0 };
        assert(ib.src);
        size_t res = ZSTD_decompressStream((ZSTD_DStream*)my->zd, &ob, &ib);
        src.advanceBuffered(ib.pos);
        if(ZSTD_isError(res))
            break;
        if(ob.pos == bytes)
            break; // output buffer full, done here
        if(!ib.size && src.done())
            break; // input exhausted
    }

    return ob.pos;
}
