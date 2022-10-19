#include <string.h>
#include <assert.h>
#include "brstream.h"
#include <brotli/encode.h>

BrotliWriteStream::BrotliWriteStream(BufferedWriteStream& sm, int level, char* buf, size_t bufsize)
    : BufferedWriteStream(NULL, _Write, buf, bufsize)
    , _sm(sm)
    , br(BrotliEncoderCreateInstance(NULL, NULL, NULL))
{
    int ok = BrotliEncoderSetParameter((BrotliEncoderState*)br, BROTLI_PARAM_QUALITY, level);
    assert(ok == BROTLI_TRUE);
    sm.init();
}

BrotliWriteStream::~BrotliWriteStream()
{
    finish();
    BrotliEncoderDestroyInstance((BrotliEncoderState*)br);
}

void BrotliWriteStream::finish()
{
    if(_dst && Tell()) // Only output something if we ever got any data
    {
        Flush();

        while(!BrotliEncoderIsFinished((BrotliEncoderState*)br))
        {
            const BufInfo buf = _sm.getBuffer();
            size_t avail_out = buf.remain;
            size_t avail_in = 0;
            u8 *next_out = (u8*)buf.buf;
            int status = BrotliEncoderCompressStream((BrotliEncoderState*)br, BROTLI_OPERATION_FINISH,
                &avail_in, NULL, &avail_out, &next_out, NULL);
            assert(status == BROTLI_TRUE);
            if(size_t packed = buf.remain - avail_out)
                _sm.advanceBuffer(packed);
        }

        _sm.Flush();
        _dst = NULL; // make sure the BufferedWriteStream dtor doesn't flush
    }
}

size_t BrotliWriteStream::_Write(const void* src, size_t bytes, BufferedWriteStream* self)
{
    BrotliWriteStream* me = static_cast<BrotliWriteStream*>(self);

    const u8 *data_in = (const u8*)src;
    size_t avail_in = bytes;

    while(avail_in)
    {
        const BufInfo buf = me->_sm.getBuffer();
        size_t avail_out = buf.remain;
        u8 *next_out = (u8*)buf.buf;
        int status = BrotliEncoderCompressStream((BrotliEncoderState*)me->br, BROTLI_OPERATION_PROCESS,
            &avail_in, &data_in, &avail_out, &next_out, NULL);
        assert(status == BROTLI_TRUE);
        if(size_t packed = buf.remain - avail_out)
            me->_sm.advanceBuffer(packed);
    }
    return bytes;
}
