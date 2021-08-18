#include "json_out.h"

// Explicit specializaton to dedup code that uses the common stream base
void writeJson(BufferedWriteStream& out, const VarCRef src, bool pretty)
{
    out.init();
    writeJson_T(out, src, pretty);
}

std::string dumpjson(VarCRef ref, bool pretty)
{
    rapidjson::StringBuffer sb;
    writeJson_T(sb, ref, pretty);
    sb.Flush();
    return sb.GetString();
}
