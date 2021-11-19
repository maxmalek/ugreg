#include "handler_debug.h"
#include <sstream>
#include <string>
#include "debugfunc.h"
#include "json_out.h"

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
        std::shared_lock lock(_tree.mutex);
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
        std::shared_lock lock(_tree.mutex);
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

        std::shared_lock lock(_tree.mutex);
        _tree.collateFree(coll);
    }

    return 0;
}

DebugCleanupHandler::DebugCleanupHandler(DataTree& tree, const char* prefix)
    : RequestHandler(prefix), _tree(tree)
{
}

DebugCleanupHandler::~DebugCleanupHandler()
{
}

int DebugCleanupHandler::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    std::unique_lock lock(_tree.mutex);

    char buf[64];

    for (size_t i = 0; i < _toclean.size(); ++i)
        _toclean[i]->clearCache();
    sprintf(buf, "--- %u caches cleared ---\n", (unsigned)_toclean.size());
    writeStr(dst, buf);

    std::vector<Var*> todo;
    _tree.fillExpiredSubnodes(todo);

    sprintf(buf, "--- %u subnodes have expired: ---\n", (unsigned)todo.size());
    writeStr(dst, buf);

    for (size_t i = 0; i < todo.size(); ++i)
    {
        writeStr(dst, dumpjson(VarCRef(_tree, todo[i]), true).c_str());
        todo[i]->clear(_tree);
    }

    writeStr(dst, "--- defrag... ---\n");

    _tree.defrag();

    writeStr(dst, "--- Done. ---\n");
    return 0;
}
