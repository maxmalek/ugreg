#include "json_out.h"

// Explicit specializaton to dedup code that uses the common stream base
void writeJson(BufferedWriteStream& out, const VarCRef src, bool pretty)
{
    out.init();
    writeJson_T(out, src, pretty);
}
