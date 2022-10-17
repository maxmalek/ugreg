#include "handler_debug.h"
#include <sstream>
#include <string>
#include "debugfunc.h"
#include "json_out.h"

InfoHandler::InfoHandler(const DataTree& tree, const char* prefix)
    : RequestHandler(prefix, NULL), _tree(tree)
{
}

InfoHandler::~InfoHandler()
{
}

int InfoHandler::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    dst.WriteStr("--- memory stats of main tree ---\n");
    std::string out;
    {
        std::ostringstream os;
        std::shared_lock lock(_tree.mutex);
        dumpAllocInfoToString(os, _tree);
        out = os.str();
    }
    dst.Write(out.c_str(), out.length());
    return 0;
}

DebugStrpoolHandler::DebugStrpoolHandler(const DataTree& tree, const char* prefix)
    : RequestHandler(prefix, NULL), _tree(tree)
{
}

DebugStrpoolHandler::~DebugStrpoolHandler()
{
}

int DebugStrpoolHandler::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    StringPool::StrColl coll;

    {
        std::shared_lock lock(_tree.mutex);
        coll = _tree.collate();
    }

    char buf[64];
    sprintf(buf, "--- %u strings in pool ---\n", (unsigned)coll.size());
    dst.WriteStr(buf);

    for(size_t i = 0; i < coll.size(); ++i)
    {
        dst.Write(coll[i].s.c_str(), coll[i].s.length());
        dst.Put('\n');
    }

    return 0;
}

DebugCleanupHandler::DebugCleanupHandler(DataTree& tree, const char* prefix)
    : RequestHandler(prefix, NULL), _tree(tree)
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
    dst.WriteStr(buf);

    std::vector<Var*> todo;
    _tree.fillExpiredSubnodes(todo);

    sprintf(buf, "--- %u subnodes have expired: ---\n", (unsigned)todo.size());
    dst.WriteStr(buf);

    for (size_t i = 0; i < todo.size(); ++i)
    {
        dst.WriteStr(dumpjson(VarCRef(_tree, todo[i]), true).c_str());
        todo[i]->clear(_tree);
    }

    dst.WriteStr("--- defrag... ---\n");

    _tree.defrag();

    dst.WriteStr("--- Done. ---\n");
    return 0;
}
