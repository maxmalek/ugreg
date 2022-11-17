#pragma once

// (De-)Serialization functions with optional format detection

#include "jsonstreamwrapper.h"
#include "variant.h"

namespace serialize {

enum Format
{
    AUTO,
    JSON,
    BJ
};

enum Compression
{
    AUTOC,
    RAW,
    ZSTD
};

bool load(VarRef dst, const char *fn, Compression comp = AUTOC, Format fmt = AUTO);
bool load(VarRef dst, BufferedReadStream& rs, Format fmt = AUTO);

bool save(const char *fn, VarCRef src, Compression comp = AUTOC, Format fmt = AUTO, int level = 3);
bool save(BufferedWriteStream& ws, VarCRef src, Format fmt);

} // end namespace serialize
