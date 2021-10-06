#include "handler_debug.h"
#include <sstream>
#include <string>
#include "debugfunc.h"

InfoHandler::InfoHandler(const DataTree& tree, const char* prefix)
    : RequestHandler(prefix), _tree(tree)
{
}

InfoHandler::~InfoHandler()
{
}

static void writeStr(BufferedWriteStream& out, const char* s)
{
    while (*s)
        out.Put(*s++);
}

int InfoHandler::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    writeStr(dst, "--- memory stats of main tree ---\n");
    std::string out;
    {
        std::ostringstream os;
        std::shared_lock<std::shared_mutex> lock(_tree.mutex);
        dumpAllocInfoToString(os, _tree);
        out = os.str();
    }
    writeStr(dst, out.c_str());
    return 0;
}
