#include <string.h>
#include <assert.h>
#include "zstream.h"
#include "miniz.h"

DeflateWriteStream::DeflateWriteStream(BufferedWriteStream& sm, int level, char* buf, size_t bufsize)
    : BufferedWriteStream(NULL, _Write, buf, bufsize)
    , _sm(sm)
{
    sm.init();
    memset(&z, 0, sizeof(z));
    int status = mz_deflateInit(&z, level);
    assert(status == MZ_OK);
}

DeflateWriteStream::~DeflateWriteStream()
{
    finish();
    mz_deflateEnd(&z);
}

void DeflateWriteStream::finish()
{
    if(_dst && z.total_in) // Only output something if we ever got any data
    {
        Flush();
        z.next_in = NULL;
        z.avail_in = 0;
        int status;
        do
            status = packloop(MZ_FINISH);
        while (status != MZ_STREAM_END); // make sure everything is flushed
    }
    _dst = NULL; // make sure the BufferedWriteStream dtor doesn't flush
}

int DeflateWriteStream::packloop(int flush)
{
    int status;
    do
    {
        const BufInfo b = _sm.getBuffer();
        z.next_out = (unsigned char*)b.buf;
        z.avail_out = b.remain;

        status = mz_deflate(&z, flush);
         if(status < 0) // error? gtfo
            break;
        assert(status == MZ_OK || (flush == MZ_FINISH && status == MZ_STREAM_END));

        // The packer may or may not output bytes. If it does, forward them to the underlying stream
        if(size_t packed = b.remain - z.avail_out)
            _sm.advanceBuffer(packed);
    }
    while (z.avail_in);
    return status;
}

size_t DeflateWriteStream::_Write(const void* src, size_t bytes, BufferedWriteStream* self)
{
    DeflateWriteStream* me = static_cast<DeflateWriteStream*>(self);
    mz_streamp z = &me->z;
    z->next_in = (const unsigned char*)src;
    z->avail_in = bytes;
    me->packloop(MZ_NO_FLUSH);
    assert(z->avail_in == 0); // make sure everything was consumed
    return bytes;
}
