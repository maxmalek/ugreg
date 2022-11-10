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
