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

Format formatFromFileName(const char *fn); // returns AUTO when we can't deduct it from the file extension

bool load(VarRef dst, const char *fn, Format fmt = AUTO);
bool load(VarRef dst, BufferedReadStream& rs, Format fmt = AUTO);

bool save(const char *fn, VarCRef src, Format fmt = AUTO);
bool save(BufferedWriteStream& ws, VarCRef src, Format fmt = AUTO);

} // end namespace serialize
