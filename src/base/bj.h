#pragma once

// BJ - compactest binary JSONlike encoding

#include "variant.h"

class BufferedReadStream;
class BufferedWriteStream;


namespace bj {

struct Limits
{
    // max size of one element (array elements, map pairs, string length)
    size_t maxsize = 1024 * 1024 * 1024;
    // max size of constants table
    size_t constants = 256 * 1024 * 1024;
};

// tmpalloc is for string pooling. Optional but recommended.
size_t encode(BufferedWriteStream& dst, const VarCRef& json, BlockAllocator *tmpalloc);

bool  decode_json(VarRef dst, BufferedReadStream& src, const Limits& lim = Limits());

bool checkMagic4(const char *p);

} // end namespace bj
