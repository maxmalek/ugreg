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
#include "zstream.h"
#include "config.h"


TreeHandler::TreeHandler(DataTree& tree, size_t skipFromRequest, const ServerConfig& cfg )
    : tree(tree), _skipFromRequest(skipFromRequest)
    , _cache(cfg.reply_cache.rows, cfg.reply_cache.columns)
    , cfg(cfg)
{
}

TreeHandler::~TreeHandler()
{
}

int TreeHandler::Handler(mg_connection* conn, void* self)
{
    return static_cast<TreeHandler*>(self)->onRequest(conn);
}

static const char s_defaultChunkedOK[] =
"HTTP/1.1 200 OK\r\n"
"Cache-Control: no-cache, no-store, must-revalidate, private, max-age=0\r\n"
"Expires: 0\r\n"
"Pragma: no-cache\r\n"
"Content-Type: text/json; charset=utf-8\r\n"
"Transfer-Encoding: chunked\r\n"
"Connection: close\r\n"
"\r\n";

static const char s_cachedOKStart[] =
"HTTP/1.1 200 OK\r\n"
"Cache-Control: no-cache, no-store, must-revalidate, private, max-age=0\r\n"
"Expires: 0\r\n"
"Pragma: no-cache\r\n"
"Content-Type: text/json; charset=utf-8\r\n"
"Connection: close\r\n"
"Content-Length: "; // + bytes + \r\n

static const char s_cachedDeflateOKStart[] =
"HTTP/1.1 200 OK\r\n"
"Cache-Control: no-cache, no-store, must-revalidate, private, max-age=0\r\n"
"Expires: 0\r\n"
"Pragma: no-cache\r\n"
"Content-Type: text/json; charset=utf-8\r\n"
"Connection: close\r\n"
"Content-Encoding: deflate\r\n"
"Content-Length: "; // + bytes + \r\n


typedef void (*RequestBufferWriter)(BufferedWriteStream& ws, VarCRef sub, const Request& r);

static void _finalizeStoredRequest(BufferedWriteStream& ws, VarCRef sub, const Request& r)
{
    writeJson(ws, sub, !!(r.flags & RQF_PRETTY));
}

static void _finalizeDeflateRequest(BufferedWriteStream& ws, VarCRef sub, const Request& r)
{
    char zbuf[8 * 1024];
    DeflateWriteStream z(ws, 1, zbuf, sizeof(zbuf)); // TODO: use compression level from config
    return _finalizeStoredRequest(z, sub, r);
}

struct CachedRequestOutHandler
{
    const char* header;
    size_t headerSize;
    RequestBufferWriter writer;
};

// Key: CompressionType (see request.h)
static const CachedRequestOutHandler s_requestOut[] =
{
    { s_cachedOKStart,        sizeof(s_cachedOKStart),        _finalizeStoredRequest },
    { s_cachedDeflateOKStart, sizeof(s_cachedDeflateOKStart), _finalizeDeflateRequest }
};


// mg_send_http_ok() is a freaking performance hog.
// in case everything is fine, just poop out an ok http header and move on.
static void sendDefaultChunkedOK(mg_connection *conn)
{
    mg_write(conn, s_defaultChunkedOK, sizeof(s_defaultChunkedOK) - 1); // don't include the \0
}


static int sendStoredRequest(mg_connection* conn, const CountedPtr<const StoredReply>& srq, const Request& r)
{
    const char *hdr    = s_requestOut[r.compression].header;
    const size_t hdrsz = s_requestOut[r.compression].headerSize;

    mg_write(conn, hdr, hdrsz - 1);

    const size_t len = srq->body.size();
    char buf[64];

    // equivalent to:
    // size_t partlen = sprintf(buf, "%u\r\n\r\n", (unsigned)len);
    // char *p = &buf[0];
    // ...but much faster
    char *p = sizetostr_unsafe(buf, sizeof(buf) - 3, len); // leave 3 extra byte at the end unused
    assert(buf[sizeof(buf) - 4] == 0);
    buf[sizeof(buf)-4] = '\r'; // overwrite the \0 at the end of the string
    buf[sizeof(buf)-3] = '\n'; // overwrite the 3 leftover bytes
    buf[sizeof(buf)-2] = '\r';
    buf[sizeof(buf)-1] = '\n';
    const size_t partlen = &buf[sizeof(buf)] - p;

    mg_write(conn, p, partlen);

    mg_write(conn, srq->body.data(), len);
    return 200;
}

static int sendToSocketNoCache(mg_connection *conn, VarCRef sub)
{
    //mg_send_http_ok(conn, "text/json", -1);
    sendDefaultChunkedOK(conn);

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

static StoredReply *prepareStoredRequest(VarCRef sub, const Request& r)
{
    char buf[8 * 1024];
    StoredReply* rq = new StoredReply;

    // Extra scope to make sure the stream is destroyed and flushes all its data before we finalize rq
    {
        StoredReplyWriteStream wr(rq, buf, sizeof(buf));
        s_requestOut[r.compression].writer(wr, sub, r);
    }

    // OPTIMIZE: this could be moved to be done in writeJson() while we walk the tree
    rq->expiryTime = getTreeMinExpiryTime(sub);

    //rq->body.shrink_to_fit(); // Don't do this here! We're holding a perf-critical lock, this can wait.
    return rq;
}

// This is called from many threads at once.
// Avoid anything that changes the tree, and ensure proper read-locking!
int TreeHandler::onRequest(mg_connection* conn)
{
    const mg_request_info* info = mg_get_request_info(conn);

    Cache::Key k;
    {
        Request r;
        if(!r.parse(info, _skipFromRequest))
        {
            mg_send_http_error(conn, 400, ""); // Bad Request
            return 400;
        }
        k = std::move(r);
    }

    CountedPtr<const StoredReply> srq;
    if(_cache.enabled)
    {
        srq = _cache.get(k);
        if(srq)
            if(!srq->expiryTime || timeNowMS() < srq->expiryTime)
                return sendStoredRequest(conn, srq, k.obj);
    }


    // ---- BEGIN READ LOCK ----
    // Tree query and use of the result must be locked.
    // Can't risk a merge or expire process to drop parts of the tree in another thread that we're still using here.
    StoredReply *rq;
    {
        std::shared_lock<std::shared_mutex> lock(tree.mutex);

        VarCRef sub = tree.subtree(k.obj.query.c_str());
        //printf("sub = %p\n", sub.v);
        if(!sub)
        {
            mg_send_http_error(conn, 404, "");
            return 404;
        }

        if(!_cache.enabled)
            return sendToSocketNoCache(conn, sub);

        rq = prepareStoredRequest(sub, k.obj); // this needs to be locked because we use sub...
    }
    // ... but once the reply was prepared, the lock is no longer necessary

    rq->body.shrink_to_fit();

    // When a max. cache time is set, see whether the object expires on its own earlier than
    // the maxtime. Adjust accordinly.
    if(cfg.reply_cache.maxtime)
    {
        u64 t = rq->expiryTime;
        u64 maxt = timeNowMS() + cfg.reply_cache.maxtime;
        t = t ? std::min(t, maxt) : maxt;
        rq->expiryTime = t;
    }

    printf("NEW CACHE: compr=%u, size=%u, expiry=%" PRIu64 "\n",
        k.obj.compression, (unsigned)rq->body.size(), rq->expiryTime);

    // Pin it. Important!
    // Some other thread may evict the cache entry in the meantime,
    // which would delete the object if we didn't hold a CountedPtr to it,
    // in which case rq would become a dangling pointer. Bad!
    srq = rq;

    _cache.put(k, srq);
    return sendStoredRequest(conn, srq, k.obj);
}
