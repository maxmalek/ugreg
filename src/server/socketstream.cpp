#include "socketstream.h"
#include "civetweb/civetweb.h"
#include <assert.h>

SocketWriteStream::SocketWriteStream(void* conn, char* buf, size_t bufsize, const char* hdr, size_t hdrsize)
    : BufferedWriteStream(NULL, _WriteInit, buf, bufsize), _conn(conn), _hdr(hdr), _hdrsize(hdrsize)
{
}

size_t SocketWriteStream::_WriteInit(const void* src, size_t bytes, BufferedWriteStream* self)
{
    SocketWriteStream* me = static_cast<SocketWriteStream*>(self);
    mg_connection* conn = static_cast<mg_connection*>(me->_conn);
    if(me->_hdrsize)
        if(mg_write(conn, me->_hdr, me->_hdrsize) <= 0)
            return 0;
    me->_writef = _WriteChunked;
    return _WriteChunked(src, bytes, self);
}

size_t SocketWriteStream::_WriteChunked(const void* src, size_t bytes, BufferedWriteStream* self)
{
    assert(bytes);
    SocketWriteStream* me = static_cast<SocketWriteStream*>(self);
    mg_connection* conn = static_cast<mg_connection*>(me->_conn);
    return mg_send_chunk(conn, (const char*)src, (unsigned)bytes) > 0
        ? bytes : 0;
}

ThrowingSocketWriteStream::ThrowingSocketWriteStream(void* conn, char* buf, size_t bufsize, const char* hdr, size_t hdrsize)
    : BufferedWriteStream(NULL, _WriteInit, buf, bufsize), _conn(conn), _hdr(hdr), _hdrsize(hdrsize)
{
}

ThrowingSocketWriteStream::~ThrowingSocketWriteStream()
{
    try // make sure this won't throw in the parent dtor
    {
        Flush();
    }
    catch(WriteFail) {}
}

size_t ThrowingSocketWriteStream::_WriteInit(const void* src, size_t bytes, BufferedWriteStream* self)
{
    ThrowingSocketWriteStream* me = static_cast<ThrowingSocketWriteStream*>(self);
    mg_connection* conn = static_cast<mg_connection*>(me->_conn);
    if (me->_hdrsize)
        mg_write(conn, me->_hdr, me->_hdrsize);
    me->_writef = _WriteChunked;
    return _WriteChunked(src, bytes, self);
}

size_t ThrowingSocketWriteStream::_WriteChunked(const void* src, size_t bytes, BufferedWriteStream* self)
{
    assert(bytes);
    ThrowingSocketWriteStream* me = static_cast<ThrowingSocketWriteStream*>(self);
    mg_connection* conn = static_cast<mg_connection*>(me->_conn);
    if(mg_send_chunk(conn, (const char*)src, (unsigned)bytes) <= 0)
    {
        me->_err = true;
        WriteFail ex { self->Tell() };
        throw ex;
    }

    return bytes;
}
