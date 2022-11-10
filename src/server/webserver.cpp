#include <string.h>
#include <sstream>
#include <assert.h>
#include "webserver.h"
#include "civetweb/civetweb.h"
#include "config.h"
#include "socketstream.h"
#include "treefunc.h"
#include "zstream.h"
#include "brstream.h"
#include "zstdstream.h"
#include "json_out.h"


WebServer::WebServer()
    : _ctx(NULL)
{
}

WebServer::~WebServer()
{
    stop();
    for(size_t i = 0; i < _ownedHandlers.size(); ++i)
        delete _ownedHandlers[i];
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
    assert(entrypoint && entrypoint[0] == '/');
    if(_ctx)
        mg_set_request_handler(_ctx, entrypoint, h, ud);
    else
    {
        StoredHandler sh;
        sh.func = h;
        sh.ep = entrypoint;
        sh.ud = ud;
        _storedHandlers.push_back(std::move(sh));
    }
}

void WebServer::registerHandler(const RequestHandler& h)
{
    registerHandler(h.prefix(), h.Handler, (void*)&h);
}

void WebServer::registerHandler(RequestHandler* h, bool own)
{
    registerHandler(h->prefix(), h->Handler, (void*)h);
    if(own)
    {
        bool add = true;
        for (size_t i = 0; i < _ownedHandlers.size(); ++i)
            if(_ownedHandlers[i] == h)
            {
                add = false;
                break;
            }
        if(add)
            _ownedHandlers.push_back(h);
    }
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
        "additional_header", "Access-Control-Allow-Origin: *",
        NULL, NULL,
        NULL
    };

    if(cfg.cert.length())
    {
        printf("WS: ssl_certificate = '%s'\n", cfg.cert.c_str());
        options[6] = "ssl_certificate";
        options[7] = cfg.cert.c_str();
    }

    mg_context *ctx = mg_start(&cb, NULL, options);
    if(!ctx)
    {
        printf("mg_start() failed! (Is something already listening on our port?)\n");
        return false;
    }

    _ctx = ctx;

    for(size_t i = 0; i < _storedHandlers.size(); ++i)
    {
        const StoredHandler& sh = _storedHandlers[i];
        mg_set_request_handler(ctx, sh.ep.c_str(), sh.func, sh.ud);
    }
    _storedHandlers.clear();

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

struct HeaderHelper
{
    enum { MAX_SIZE = 128 };
    char buf[MAX_SIZE];
    size_t size;

    HeaderHelper(CompressionType c, size_t sz)
    {
        char *p = buf;
#define hhADD(s) { memcpy(p, s, (sizeof(s) - 1)); p += (sizeof(s) - 1); }
#define hhCRLF { *p++ = '\r'; *p++ = '\n'; }
        if(c)
        {
            hhADD("Content-Encoding: ");
            for(const char *x = CompressionTypeName[c]; *x; )
                *p++ = *x++;
            hhCRLF;
        }
        if(sz)
        {
            hhADD("Content-Length: ");
            char nb[32];
            char* n = sizetostr_unsafe(nb, sizeof(nb), sz);
            for (; *n; )
                *p++ = *n++;
            hhCRLF;
        }
        else
            hhADD("Transfer-Encoding: chunked\r\n");
        hhCRLF;
        *p = 0;
        size = p - &buf[0];
        assert(size < MAX_SIZE && size == strlen(buf));
#undef hhADD
#undef hhCRLF
    }
};

const RequestHandler::StreamWriteMth RequestHandler::s_writer[COMPR_ARRAYSIZE] =
{
    /* COMPR_NONE */     &RequestHandler::onRequest,
    /* COMPR_ZSTD */     &RequestHandler::onRequest_zstd,
    /* COMPR_BROTLI */   &RequestHandler::onRequest_brotli,
    /* COMPR_DEFLATE */  &RequestHandler::onRequest_deflate,
};

int RequestHandler::Handler(mg_connection* conn, void* self)
{
    return static_cast<const RequestHandler*>(self)->_onRequest(conn);
}

RequestHandler::RequestHandler(const char* prefix, const char *mimetype)
    : myPrefix(prefix), maxcachetime(0)
{
    prepareHeader(mimetype);
}

RequestHandler::~RequestHandler()
{
}

// FIXME: re-integrate expirytime!!

void RequestHandler::setupCache(u32 rows, u32 cols, u64 maxtime)
{
    _cache.resize(rows, cols);
    maxcachetime = maxtime;
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
                // prepend headers before the actual (possibly compressed) payload
                HeaderHelper hh(k.obj.compression, 0);
                std::vector<char> hdrbuf(preparedHdr.size() + hh.size);
                memcpy(&hdrbuf[0], preparedHdr.c_str(), preparedHdr.size());
                memcpy(&hdrbuf[preparedHdr.size()], hh.buf, hh.size);

                ThrowingSocketWriteStream wr(conn, buf, sizeof(buf), hdrbuf.data(), hdrbuf.size());
                wr.init();
                const StreamWriteMth writer = s_writer[k.obj.compression];

                status = (this->*writer)(wr, conn, k.obj);
            }
            if(!status) // 0 means the handler didn't error out
            {
                mg_send_chunk(conn, "", 0); // terminating chunk
                status = 200; // all good
            }
            else if(status == HANDLER_FALLTHROUGH)
                status = 0; // on 0 return, civetweb falls through to the next handler
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
        StoredReply *rq = new StoredReply(hdrReserveSize);

        // Pin it so that it's deleted if we return early.
        // Also, once put, some other thread may evict the cache entry in the meantime,
        // which would delete the object if we didn't hold a CountedPtr to it,
        // in which case rq would become a dangling pointer. Bad!
        srq = rq;

        int status;
        {
            StoredReplyWriteStream wr(rq, buf, sizeof(buf));
            const StreamWriteMth writer = s_writer[k.obj.compression];
            status = (this->*writer)(wr, conn, k.obj);
        }
        if(status) // expected to return 0 if all good and not custom handled
            return status;

        HeaderHelper hh(k.obj.compression, rq->bodysize());
        if(!rq->spliceHeader(preparedHdr.c_str(), preparedHdr.size(), hh.buf, hh.size))
        {
            printf("FAILED TO CACHE: compr=%u, sizes: %u, %u, %u (header would stomp data)\n",
                k.obj.compression, (unsigned)preparedHdr.size(), (unsigned)hh.size, (unsigned)srq->fullsize());
            // should never be here, but still finish gracefully. we just can't store the reply if we messed up the header, but sending the individual parts is ok
            mg_write(conn, preparedHdr.c_str(), preparedHdr.size());
            mg_write(conn, hh.buf, hh.size);
            mg_write(conn, rq->bodydata(), rq->bodysize());
            return 200;
        }

        rq->data.shrink_to_fit();

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
            k.obj.compression, (unsigned)srq->fullsize(), srq->expiryTime);

        _cache.put(k, srq);
    }

    // when we have the complete cached request, just plonk it out entirely (the header is included)
    mg_write(conn, srq->fulldata(), srq->fullsize());
    return 200;
}

int RequestHandler::onRequest_deflate(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    char zbuf[8 * 1024];
    DeflateWriteStream z(dst, 1, zbuf, sizeof(zbuf)); // TODO: use compression level from config
    z.init();
    int ret = this->onRequest(z, conn, rq);
    z.finish();
    return ret;
}

int RequestHandler::onRequest_brotli(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    char zbuf[8 * 1024];
    BrotliWriteStream z(dst, 3, zbuf, sizeof(zbuf)); // TODO: use compression level from config
    z.init();
    int ret = this->onRequest(z, conn, rq);
    z.finish();
    return ret;
}

int RequestHandler::onRequest_zstd(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    char zbuf[8 * 1024];
    ZstdWriteStream z(dst, 3, zbuf, sizeof(zbuf)); // TODO: use compression level from config
    z.init();
    int ret = this->onRequest(z, conn, rq);
    z.finish();
    return ret;
}

void RequestHandler::clearCache()
{
    _cache.clear();
}

// mg_send_http_ok() is a freaking performance hog, so we prepare the most common headers early.
// in case everything is fine, just poop out an ok http header and move on.
void RequestHandler::prepareHeader(const char* mimetype)
{
    if(!mimetype || !*mimetype)
        mimetype = "text/plain; encoding=utf8";

        std::ostringstream os;
    os << "HTTP/1.1 200 OK\r\n"
            "Cache-Control: no-cache, no-store, must-revalidate, private, max-age=0\r\n"
            "Expires: 0\r\n"
            "Pragma: no-cache\r\n"
            "Content-Type: " << mimetype << "\r\n";
    preparedHdr = os.str();

    hdrReserveSize = (preparedHdr.size() + HeaderHelper::MAX_SIZE + 63) & ~63; // align body start to cache line
}
