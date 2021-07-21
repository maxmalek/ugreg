#include "handler_get.h"
#include <sstream>
#include <stdio.h>
#include "civetweb/civetweb.h"
#include "rapidjson/filereadstream.h"
#include "json_in.h"
#include "json_out.h"
#include "jsonstreamwrapper.h"
#include "subproc.h"
#include "socketstream.h"


TreeHandler::TreeHandler(DataTree& tree, size_t skipFromRequest)
    : tree(tree), _skipFromRequest(skipFromRequest)
{
}

TreeHandler::~TreeHandler()
{
}

int TreeHandler::Handler(mg_connection* conn, void* self)
{
    return static_cast<TreeHandler*>(self)->onRequest(conn);
}

static const char s_defaultOK[] =
"HTTP/1.1 200 OK\r\n"
"Cache-Control: no-cache, no-store, must-revalidate, private, max-age=0\r\n"
"Expires: 0\r\n"
"Pragma: no-cache\r\n"
"Content-Type: text/json; charset=utf-8\r\n"
"Transfer-Encoding: chunked\r\n"
"Connection: close\r\n"
"\r\n";

// mg_send_http_ok() is a freaking performance hog.
// in case everything is fine, just poop out an ok http header and move on.
static void sendDefaultOK(mg_connection *conn)
{
    mg_write(conn, s_defaultOK, sizeof(s_defaultOK) - 1); // don't include the \0
}


// This is called from many threads at once.
// Avoid anything that changes the tree.
int TreeHandler::onRequest(mg_connection* conn)
{
    const mg_request_info* info = mg_get_request_info(conn);

    const char* q = info->local_uri + _skipFromRequest;

    /*std::ostringstream os;
    os << q;
    if (info->query_string)
        os << " -- " << info->query_string;

    std::string out = os.str();
    printf("q = [%s]\n", out.c_str());*/

    VarCRef sub = tree.subtree(q);
    //printf("sub = %p\n", sub.v);
    if(!sub)
    {
        mg_send_http_error(conn, 404, "");
        return 404;
    }

    //mg_send_http_ok(conn, "text/json", -1);
    sendDefaultOK(conn);

    try
    {
        char buf[32*1024];
        ThrowingSocketWriteStream wr(conn, buf, sizeof(buf));
        writeJson(wr, sub, false);
        mg_send_chunk(conn, "", 0); // terminating chunk
        //printf("[%s] JSON reply sent\n", out.c_str());
    }
    catch(ThrowingSocketWriteStream::WriteFail ex)
    {
        //printf("[%s] Wrote %u bytes to socket, then failed (client aborted?)\n", out.c_str(), (unsigned)ex.written);
    }
    catch(...)
    {
        //printf("[%s] Unhandled exception\n", out.c_str());
    }

    return 200; // HTTP OK
}
