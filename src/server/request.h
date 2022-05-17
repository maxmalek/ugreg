#pragma once

#include <string>
#include <vector>
#include "refcounted.h"
#include "jsonstreamwrapper.h"

struct mg_request_info;
struct mg_connection;
class VarRef;

enum RequestType
{
    RQ_UNKNOWN,
    RQ_GET,
    RQ_POST,
    RQ_HEAD,
    RQ_OPTIONS,
    RQ_PUT,
    RQ_DELETE,
};

// Supported compression algorithms, ordered by preference
// Higher values are preferred
// See request.cpp: parseEncoding()
enum CompressionType
{
    COMPR_NONE,
    COMPR_DEFLATE,

    COMPR_ARRAYSIZE
};
// name used in HTTP header Content-Enc oding field
static const char* CompressionTypeName[] =
{
    NULL,
    "deflate"
};

enum RequestFlags // bitmask
{
    RQF_NONE   = 0x00,
    RQF_PRETTY = 0x01,
};
inline static RequestFlags operator|(RequestFlags a, RequestFlags b) { return RequestFlags(unsigned(a) | unsigned(b)); }
inline static RequestFlags& operator|=(RequestFlags& a, RequestFlags b) { return (a = RequestFlags(unsigned(a) | unsigned(b))); }


enum RequestFormat
{
    RQFMT_DEFAULT = 0,
    RQFMT_JSON,
};

static const char* RequestFormatName[] =
{
    "default",
    "json"
};

// One request contains everything needed to generate the response.
// The request is also used as a key for the response cache, so make sure
// that operator== does a proper equality check of ALL members that may modify
// the response that is generated.
class Request
{
public:
    Request()
        : compression(COMPR_NONE), flags(RQF_NONE), fmt(RQFMT_DEFAULT), type(RQ_UNKNOWN) {}

    Request(const char* q, CompressionType compression, RequestFlags flags, RequestFormat f)
        : query(q), compression(compression), flags(flags), fmt(f), type(RQ_UNKNOWN) {}

    // can optionally parse params if needed. converts *params to map.
    bool parse(const mg_request_info *info, size_t skipFromQuery);

    std::string query, authorization;
    CompressionType compression;
    RequestFlags flags;
    RequestFormat fmt;
    RequestType type;

    bool operator==(const Request& o) const
    {
        return query == o.query && compression == o.compression && flags == o.flags
            && fmt == o.fmt && type == o.type && authorization == o.authorization;
    }

    static u32 Hash(const Request& r);

    // try and combine all 3 variants below to read variables.
    // json body first, then form data, then query.
    // in case of conflicts, the last read var wins.
    static int AutoReadVars(VarRef dst, mg_connection *conn);

    static int ReadFormDataVars(VarRef dst, mg_connection *conn);
    static int ReadQueryVars(VarRef dst, const mg_request_info *info);

    // pass ignoreMIME = true to load even if Content-Type isn't "application/json"
    // result must eval to a map unless acceptNotMap == true
    static int ReadJsonBodyVars(VarRef dst, mg_connection* conn, bool ignoreMIME = false, bool acceptNotMap = false);
};

// !! MUST be allocated with new when you use this!
// The basic idea behind this class is to encapsulate a HTTP header + body in such a way that it can be stored in a (lock-free, refcounting) cache.
// Since the HTTP header is included but its Content-Length field is only known once the body was fully processed,
// we reserve some space for the header up-front and splice it in once everything is done.
// The final reply can then be written to a socket in a single call with no further work required.
struct StoredReply : public Refcounted
{
    StoredReply(size_t reserveHeader) : expiryTime(0), data(reserveHeader), hdrstart(0), reservedHeaderSpace(reserveHeader) {}
    virtual ~StoredReply() {}

    u64 expiryTime;
    std::vector<char> data;
    size_t hdrstart;
    size_t reservedHeaderSpace;

    bool spliceHeader(const char* hdr1, size_t sz1, const char *hdr2, size_t sz2); // to be called max. once!

    inline const char *fulldata() const { return data.data() + hdrstart; }
    inline size_t fullsize() const { return data.size() - hdrstart; }
    inline const char *bodydata() const { return fulldata() + reservedHeaderSpace; }
    inline size_t bodysize() const { return data.size() - reservedHeaderSpace; }
};

// TODO: remove requirement for external buffer to reduce copying
class StoredReplyWriteStream : public BufferedWriteStream
{
public:
    StoredReplyWriteStream(StoredReply *req, char* buf, size_t bufsize); // mg_connection
private:
    static size_t _Write(const void* src, size_t bytes, BufferedWriteStream* self);
    StoredReply* const _req;
};
