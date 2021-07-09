#pragma once

#include "datatree.h"

class BufferedReadStream;

 // one-shot loader. replaces tree contents on success. shreds json.
bool loadJsonDestructive(Var& dst, BufferedReadStream& stream);
