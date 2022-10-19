#pragma once

// BJ - compactest binary JSONlike encoding

#include "variant.h"

class BufferedReadStream;
class BufferedWriteStream;


namespace bj {


size_t encode(BufferedWriteStream& dst, const VarCRef& json);
bool  decode_json(VarRef dst, BufferedReadStream& src);

} // end namespace bj
