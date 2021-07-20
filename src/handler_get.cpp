#include "handler_get.h"
#include <sstream>
#include <stdio.h>
#include "civetweb/civetweb.h"
#include "rapidjson/rapidjson.h"
#include "rapidjson/filereadstream.h"
#include "json_in.h"
#include "json_out.h"
#include "accessor.h"
#include "jsonstreamwrapper.h"
#include "subproc.h"
#include "socketstream.h"


static void jsonout(VarCRef ref)
{
    rapidjson::StringBuffer sb;
    writeJson_T(sb, ref, false);
    sb.Flush();
    puts(sb.GetString());
}

TreeHandler::TreeHandler(size_t skipFromRequest)
    : _skipFromRequest(skipFromRequest)
{
    
    {
        char json[] = " { \"a\":[1, 2, 3, [], {}, [0]], \"b\":null, \"c\":123.45 } ";
        //char json[] = " { \"a\":[1, 2, 3, [], {}, [0]] } ";
        char json2[] = "{ \"b\":\"BBB\", \"c\":[0], \"d\":[{}, 987.123] }";
        InplaceStringStream jsonss(&json[0], sizeof(json));
        InplaceStringStream jsonss2(&json2[0], sizeof(json2));

        DataTree extra;
        bool ok2 = loadJsonDestructive(extra.root(), jsonss2);
        assert(ok2);

        bool ok = loadJsonDestructive(tree.root(), jsonss);
        assert(ok);

        extra.root()["e"]["first"] = "<---";
        tree.root()["e"]["second"] = "--->";

        jsonout(tree.root());
        jsonout(extra.root());


        tree.root().merge(extra.root(), true);
        jsonout(tree.root());
    }

    {
        Accessor acc(tree, "d", 1);
        const double* v = tree.subtree(acc).asFloat();
        printf("acc: json.d.1 = %f\n", *v);

        v = tree.subtree("/d/1").asFloat();
        printf("ptr: /d/1 = %f\n", *v);
    }

    tree.root().clear();

    puts("Start bg process...");
    AsyncLaunchConfig cfg;
    cfg.args.push_back("test.bat");
    std::future<DataTree*> fut = loadJsonFromProcessAsync(std::move(cfg));


    puts("Loading file");


    if(FILE *f = fopen("citylots.json", "rb"))
    {
        char buf[4096];
        BufferedFILEReadStream fs(f, buf, sizeof(buf));
        bool ok = loadJsonDestructive(tree.root(), fs);
        printf("Loaded, ok = %u\n", ok);
        assert(ok);
        fclose(f);
    }

    DataTree *test = fut.get();
    if(test)
    {
        puts("... merging ...");
        tree.root()["twitter"].merge(test->root(), false);
        puts("... merged!");
        delete test;
    }
    else
        puts("... subprocess failed!");


    puts("Ready!");

}

TreeHandler::~TreeHandler()
{
}

int TreeHandler::Handler(mg_connection* conn, void* self)
{
    return static_cast<TreeHandler*>(self)->onRequest(conn);
}

// This is called from many threads at once.
// Avoid anything that changes the tree.
int TreeHandler::onRequest(mg_connection* conn)
{
    const mg_request_info* info = mg_get_request_info(conn);

    const char* q = info->local_uri + _skipFromRequest;

    std::ostringstream os;
    os << q;
    if (info->query_string)
        os << " -- " << info->query_string;

    std::string out = os.str();
    printf("q = [%s]\n", out.c_str());

    VarCRef sub = tree.subtree(q);
    printf("sub = %p\n", sub.v);
    if(!sub)
    {
        mg_send_http_error(conn, 404, "");
        return 404;
    }

    mg_send_http_ok(conn, "text/json", -1);

    try
    {
        char buf[4096];
        ThrowingSocketWriteStream wr(conn, buf, sizeof(buf));
        writeJson(wr, sub, false);
        mg_send_chunk(conn, "", 0); // terminating chunk
        printf("[%s] JSON reply sent\n", out.c_str());
    }
    catch(ThrowingSocketWriteStream::WriteFail ex)
    {
        printf("[%s] Wrote %u bytes to socket, then failed (client aborted?)\n", out.c_str(), (unsigned)ex.written);
    }
    catch(...)
    {
        printf("[%s] Unhandled exception\n", out.c_str());
    }

    return 200; // HTTP OK
}
