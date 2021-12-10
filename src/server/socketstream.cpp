#include "socketstream.h"
#include "civetweb/civetweb.h"
#include <assert.h>

SocketWriteStream::SocketWriteStream(void* conn, char* buf, size_t bufsize)
    : BufferedWriteStream(NULL, _Write, buf, bufsize), _conn(conn)
{
}

size_t SocketWriteStream::_Write(const void* src, size_t bytes, BufferedWriteStream* self)
{
    assert(bytes);
    SocketWriteStream* me = static_cast<SocketWriteStream*>(self);
    mg_connection* conn = static_cast<mg_connection*>(me->_conn);
    return mg_send_chunk(conn, (const char*)src, (unsigned)bytes) > 0
        ? bytes : 0;
}

ThrowingSocketWriteStream::ThrowingSocketWriteStream(void* conn, char* buf, size_t bufsize)
    : BufferedWriteStream(NULL, _Write, buf, bufsize), _conn(conn)
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

size_t ThrowingSocketWriteStream::_Write(const void* src, size_t bytes, BufferedWriteStream* self)
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
