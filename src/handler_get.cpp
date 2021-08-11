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
#include "treefunc.h"


TreeHandler::TreeHandler(DataTree& tree, size_t skipFromRequest)
    : tree(tree), _skipFromRequest(skipFromRequest)
    , _cache(128, 8) // TODO: move to config
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


static int sendStoredRequest(mg_connection* conn, const CountedPtr<const StoredRequest>& srq)
{
    sendDefaultOK(conn);
    mg_write(conn, srq->body.data(), srq->body.size());
    return 200;
}

static int sendToSocketNoCache(mg_connection *conn, VarCRef sub)
{
    //mg_send_http_ok(conn, "text/json", -1);
    sendDefaultOK(conn);

    try
    {
        char buf[32 * 1024];
        ThrowingSocketWriteStream wr(conn, buf, sizeof(buf));
        writeJson(wr, sub, false);
        mg_send_chunk(conn, "", 0); // terminating chunk
        //printf("[%s] JSON reply sent\n", out.c_str());
    }
    catch (ThrowingSocketWriteStream::WriteFail ex)
    {
        //printf("[%s] Wrote %u bytes to socket, then failed (client aborted?)\n", out.c_str(), (unsigned)ex.written);
    }
    catch (...)
    {
        //printf("[%s] Unhandled exception\n", out.c_str());
    }

    return 200; // HTTP OK
}

static CountedPtr<const StoredRequest> prepareStoredRequest(VarCRef sub, const TreeHandler::Cache::Key& k)
{
    char buf[8 * 1024];
    StoredRequest* rq = new StoredRequest;
    rq->expiryTime = getTreeMinExpiryTime(sub);
    StoredRequestWriteStream wr(rq, buf, sizeof(buf));
    writeJson(wr, sub, false);
    return rq;
}

// This is called from many threads at once.
// Avoid anything that changes the tree.
int TreeHandler::onRequest(mg_connection* conn)
{
    const mg_request_info* info = mg_get_request_info(conn);

    const char* q = info->local_uri + _skipFromRequest;

    const Cache::Key k(Request(q, 0, 0)); // TODO: compression, flags
    CountedPtr<const StoredRequest> srq = _cache.get(k);
    if(srq)
        if(srq->expiryTime < timeNowMS())
            return sendStoredRequest(conn, srq);


    // ---- BEGIN READ LOCK ----
    // Tree query and use of the result must be locked.
    // Can't risk a merge or expire process to drop parts of the tree in another thread that we're still using here.
    {
        std::shared_lock<std::shared_mutex> lock(tree.mutex);

        VarCRef sub = tree.subtree(q);
        //printf("sub = %p\n", sub.v);
        if(!sub)
        {
            mg_send_http_error(conn, 404, "");
            return 404;
        }
        return sendToSocketNoCache(conn, sub); // FIXME: remove this and fix the breakage!

        srq = prepareStoredRequest(sub, k); // this needs to be locked because we use sub...
    }

    // ... but once the reply was prepared and stored, the lock is no longer necessar
    // Just note that the thing we send must be a CountedPtr, because some other thread may evict the cache entry in the meantime,
    // which would delete the object if we didn't hold a CountedPtr to it!
    _cache.put(k, srq);
    return sendStoredRequest(conn, srq);
}
