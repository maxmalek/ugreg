#pragma once

#include "jsonstreamwrapper.h"
#include "miniz.h"

// Compresses incoming data and forwards those to anothre stream
class DeflateWriteStream : public BufferedWriteStream
{
public:
    DeflateWriteStream(BufferedWriteStream& sm, int level, char* buf, size_t bufsize);
    ~DeflateWriteStream();

    void finish();
private:
    int packloop(int flush);
    static size_t _Write(const void* src, size_t bytes, BufferedWriteStream* self);
    mz_stream z;
    BufferedWriteStream& _sm;
};
