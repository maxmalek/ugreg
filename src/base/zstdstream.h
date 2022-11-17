#pragma once

#include "jsonstreamwrapper.h"

// Compresses incoming data and forwards those to another stream
class ZstdWriteStream : public BufferedWriteStream
{
public:
    ZstdWriteStream(BufferedWriteStream& sm, int level, char* buf, size_t bufsize);
    ~ZstdWriteStream();

    void finish();
private:
    static size_t _Write(const void* src, size_t bytes, BufferedWriteStream* self);
    BufferedWriteStream& _sm;
    /* ZSTD_CStream* */ void * zc;
    const int _level;
};


// Reads compressed data from another stream and decompresses on the fly
class ZstdReadStream : public BufferedReadStream
{
public:
    ZstdReadStream(BufferedReadStream& src, char* buf, size_t bufsize);
    ~ZstdReadStream();
private:
    /* ZSTD_DStream* */ void * zd;
    BufferedReadStream& _src;
    static size_t _Read(void* dst, size_t bytes, BufferedReadStream* self);

};
