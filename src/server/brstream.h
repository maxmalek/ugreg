#pragma once

#include "jsonstreamwrapper.h"

// Compresses incoming data and forwards those to another stream
class BrotliWriteStream : public BufferedWriteStream
{
public:
    BrotliWriteStream(BufferedWriteStream& sm, int level, char* buf, size_t bufsize);
    ~BrotliWriteStream();

    void finish();
private:
    static size_t _Write(const void* src, size_t bytes, BufferedWriteStream* self);
    BufferedWriteStream& _sm;
    /*BrotliEncoderState*/ void * const br;
};
