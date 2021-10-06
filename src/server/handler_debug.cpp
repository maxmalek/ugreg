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

DebugStrpoolHandler::DebugStrpoolHandler(const DataTree& tree, const char* prefix)
    : RequestHandler(prefix), _tree(tree)
{
}

DebugStrpoolHandler::~DebugStrpoolHandler()
{
}

int DebugStrpoolHandler::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    char *coll = NULL;
    size_t n = 0;

    {
        std::shared_lock<std::shared_mutex> lock(_tree.mutex);
        coll = _tree.collate(&n);
    }

    char buf[64];
    sprintf(buf, "--- %u strings in pool ---\n", (unsigned)n);
    writeStr(dst, buf);

    if(coll)
    {
        const char *s = coll;
        for(size_t i = 0; i < n; ++i)
        {
            for(char c; (c = *s++); )
                dst.Put(c);
            dst.Put('\n');
        }

        std::shared_lock<std::shared_mutex> lock(_tree.mutex);
        _tree.collateFree(coll);
    }

    return 0;
}
