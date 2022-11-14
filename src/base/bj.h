#pragma once

// BJ - compactest binary JSONlike encoding

#include "variant.h"

class BufferedReadStream;
class BufferedWriteStream;


namespace bj {

// tmpalloc is for string pooling. Optional but recommended.
size_t encode(BufferedWriteStream& dst, const VarCRef& json, BlockAllocator *tmpalloc);

bool decode_json(VarRef dst, BufferedReadStream& src);

bool checkMagic4(const char *p);

} // end namespace bj
