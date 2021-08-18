#pragma once

#include "jsonstreamwrapper.h"


// Use after mg_send_http_ok(conn, "...", -1) -- this enables chunked transfer encoding
class SocketWriteStream : public BufferedWriteStream
{
public:
    SocketWriteStream(void *conn, char *buf, size_t bufsize); // mg_connection
private:
    static size_t _Write(const void* src, size_t bytes, BufferedWriteStream * self);
    void * const _conn; // mg_connection
};

class ThrowingSocketWriteStream : public BufferedWriteStream
{
public:
    struct WriteFail { size_t written; }; // this is thrown when a write fails
    ThrowingSocketWriteStream(void* conn, char* buf, size_t bufsize); // mg_connection
    ~ThrowingSocketWriteStream();
private:
    static size_t _Write(const void* src, size_t bytes, BufferedWriteStream* self);
    void* const _conn; // mg_connection
};
