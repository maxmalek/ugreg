#pragma once

#include "datatree.h"

class BufferedReadStream;

 // one-shot loader. replaces tree contents on success. shreds json.
bool loadJsonDestructive(VarRef dst, BufferedReadStream& stream);
bool loadJsonDestructive(VarRef dst, const char *data, size_t len);
