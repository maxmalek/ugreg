#include <string.h>
#include <sstream>
#include <assert.h>
#include "webserver.h"
#include "civetweb/civetweb.h"
#include "config.h"
#include "socketstream.h"
#include "treefunc.h"
#include "zstream.h"
#include "json_out.h"


WebServer::WebServer()
    : _ctx(NULL)
{
}

WebServer::~WebServer()
{
    stop();
}

void WebServer::stop()
{
    if(!_ctx)
        return;
    puts("WS: Stopping...");
    mg_stop(_ctx);
    _ctx = NULL;
}

void WebServer::registerHandler(const char *entrypoint, mg_request_handler h, void *ud)
{
    printf("WS: Register handler %s\n", entrypoint);
    assert(_ctx && entrypoint && entrypoint[0] == '/');
    mg_set_request_handler(_ctx, entrypoint, h, ud);
}

void WebServer::registerHandler(const RequestHandler& h)
{
    registerHandler(h.prefix(), h.Handler, (void*)&h);
}

bool WebServer::start(const ServerConfig& cfg)
{
    mg_callbacks cb;
    memset(&cb, 0, sizeof(cb));

    std::string listenbuf;
    {
        std::ostringstream ls;
        for(size_t i = 0; i < cfg.listen.size(); ++i)
        {
            if(i)
                ls << ',';
            const ServerConfig::Listen& lis = cfg.listen[i];
            if(lis.host.length())
                ls << lis.host << ':';
            ls << lis.port;
            if(lis.ssl)
                ls << 's';
        }
        listenbuf = ls.str();
        printf("WS: Listening on %s\n", listenbuf.c_str());
    }
    std::string threadsbuf = std::to_string(cfg.listen_threads);
    printf("WS: Using %u request worker threads\n", cfg.listen_threads);


    // via https://github.com/civetweb/civetweb/blob/master/docs/UserManual.md
    // Everything is handled programmatically, so there is no document_root
    // TODO: maybe set linger_timeout_ms to prevent DoS?
    const char* options[] =
    {
        "listening_ports", listenbuf.c_str(),
        "num_threads", threadsbuf.c_str(),
        NULL
    };

    mg_context *ctx = mg_start(&cb, NULL, options);
    if(!ctx)
    {
        printf("mg_start() failed! (Is something already listening on our port?)\n");
        return false;
    }

    _ctx = ctx;
    puts("WS: Started.");
    return true;
}

bool WebServer::StaticInit()
{
    return !!mg_init_library(0); // TODO: init TLS etc here?
}

void WebServer::StaticShutdown()
{
    mg_exit_library();
}


int RequestHandler::Handler(mg_connection* conn, void* self)
{
    return static_cast<const RequestHandler*>(self)->_onRequest(conn);
}

RequestHandler::RequestHandler(const char* prefix)
    : myPrefix(prefix), maxcachetime(0)
{
}

RequestHandler::~RequestHandler()
{
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

static const char s_defaultChunkedDeflateOK[] =
"HTTP/1.1 200 OK\r\n"
"Cache-Control: no-cache, no-store, must-revalidate, private, max-age=0\r\n"
"Expires: 0\r\n"
"Pragma: no-cache\r\n"
"Content-Type: text/json; charset=utf-8\r\n"
"Transfer-Encoding: chunked\r\n"
"Content-Encoding: deflate\r\n"
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


typedef int (RequestHandler::*StreamWriteMth)(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const;


struct RequestStreamHandler
{
    const char* header;
    size_t headerSize;
    StreamWriteMth writer;
};

// Key: CompressionType (see request.h)
static const RequestStreamHandler s_cachedOut[] =
{
    { s_cachedOKStart,        sizeof(s_cachedOKStart),        &RequestHandler::onRequest },
    { s_cachedDeflateOKStart, sizeof(s_cachedDeflateOKStart), &RequestHandler::onRequest_deflate }
};

static const RequestStreamHandler s_directOut[] =
{
    { s_defaultChunkedOK,        sizeof(s_defaultChunkedOK),        &RequestHandler::onRequest },
    { s_defaultChunkedDeflateOK, sizeof(s_defaultChunkedDeflateOK), &RequestHandler::onRequest_deflate }
};


// mg_send_http_ok() is a freaking performance hog.
// in case everything is fine, just poop out an ok http header and move on.
void RequestHandler::SendDefaultChunkedOK(mg_connection* conn)
{
    mg_write(conn, s_defaultChunkedOK, sizeof(s_defaultChunkedOK) - 1); // don't include the \0
}

/*
StoredReply* RequestHandler::PrepareStoredReply(VarCRef sub, const Request& r)
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

    //rq->body.shrink_to_fit(); // Don't do this here! This can wait until we're sure to not hold a lock.
    return rq;
}
*/

// FIXME: re-integrate expirytime!!

void RequestHandler::setupCache(u32 rows, u32 cols, u64 maxtime)
{
    _cache.resize(rows, cols);
    maxcachetime = maxtime;
}

static int sendStoredRequest(mg_connection* conn, const CountedPtr<const StoredReply>& srq, const Request& r)
{
    const char* hdr = s_cachedOut[r.compression].header;
    const size_t hdrsz = s_cachedOut[r.compression].headerSize;

    mg_write(conn, hdr, hdrsz - 1);

    const size_t len = srq->body.size();
    char buf[64];

    // equivalent to:
    // size_t partlen = sprintf(buf, "%u\r\n\r\n", (unsigned)len);
    // char *p = &buf[0];
    // ...but much faster
    char* p = sizetostr_unsafe(buf, sizeof(buf) - 3, len); // leave 3 extra byte at the end unused
    assert(buf[sizeof(buf) - 4] == 0);
    buf[sizeof(buf) - 4] = '\r'; // overwrite the \0 at the end of the string
    buf[sizeof(buf) - 3] = '\n'; // overwrite the 3 leftover bytes
    buf[sizeof(buf) - 2] = '\r';
    buf[sizeof(buf) - 1] = '\n';
    const size_t partlen = &buf[sizeof(buf)] - p;

    mg_write(conn, p, partlen);

    mg_write(conn, srq->body.data(), len);
    return 200;
}

int RequestHandler::_onRequest(mg_connection* conn) const
{
    const mg_request_info* info = mg_get_request_info(conn);

    Cache::Key k;
    {
        Request r;
        if (!r.parse(info, myPrefix.length()))
        {
            mg_send_http_error(conn, 400, ""); // Bad Request
            return 400;
        }
        k = std::move(r);
    }

    char buf[8 * 1024];

    if(!_cache.enabled())
    {
        int status = 200;
        try
        {
            {
                ThrowingSocketWriteStream wr(conn, buf, sizeof(buf), s_directOut[k.obj.compression].header, s_directOut[k.obj.compression].headerSize - 1); // don't include \0
                const StreamWriteMth writer = s_directOut[k.obj.compression].writer;
                status = (this->*writer)(wr, conn, k.obj);
                wr.Flush();
            }
            mg_send_chunk(conn, "", 0); // terminating chunk
        }
        catch (ThrowingSocketWriteStream::WriteFail ex)
        {
            const char* ip = mg_get_request_info(conn)->remote_addr;
            printf("WS: [%s] Wrote %u bytes to socket, then failed (client aborted?)\n", ip, (unsigned)ex.written);
        }
        return status;
    }
    // ----- Cache enabled below -----

    CountedPtr<const StoredReply> srq = _cache.get(k);
    if (srq && srq->expiryTime && timeNowMS() >= srq->expiryTime)
        srq = NULL;

    if(!srq)
    {
        StoredReply *rq = new StoredReply;

        // Pin it so that it's deleted if we return early.
        // Also, once put, some other thread may evict the cache entry in the meantime,
        // which would delete the object if we didn't hold a CountedPtr to it,
        // in which case rq would become a dangling pointer. Bad!
        srq = rq;

        int status;
        {
            StoredReplyWriteStream wr(rq, buf, sizeof(buf));
            const StreamWriteMth writer = s_cachedOut[k.obj.compression].writer;
            status = (this->*writer)(wr, conn, k.obj);
        }
        if(status)
            return status;
        rq->body.shrink_to_fit();

        // When a max. cache time is set, see whether the object expires on its own earlier than
        // the maxtime. Adjust accordingly.
        if (maxcachetime)
        {
            u64 t = rq->expiryTime;
            u64 maxt = timeNowMS() + maxcachetime;
            t = t ? std::min(t, maxt) : maxt;
            rq->expiryTime = t;
        }

        printf("NEW CACHE: compr=%u, size=%u, expiry=%" PRIu64 "\n",
            k.obj.compression, (unsigned)srq->body.size(), srq->expiryTime);

        _cache.put(k, srq);
    }

    return sendStoredRequest(conn, srq, k.obj);
}

int RequestHandler::onRequest_deflate(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    char zbuf[8 * 1024];
    DeflateWriteStream z(dst, 1, zbuf, sizeof(zbuf)); // TODO: use compression level from config
    return this->onRequest(z, conn, rq);
}
