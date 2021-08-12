#pragma once

#include <string>
#include <vector>
#include "refcounted.h"
#include "jsonstreamwrapper.h"

enum CompressionType
{
    COMPR_NONE,
    COMPR_DEFLATE
};

enum RequestFlags // bitmask
{
    RQF_NONE   = 0x00,
    RQF_PRETTY = 0x01,
};

class Request
{
public:
    Request()
        : compression(COMPR_NONE), flags(RQF_NONE) {}

    Request(const char* q, CompressionType compression, RequestFlags flags)
        : query(q), compression(compression), flags(flags) {}

    std::string query;
    CompressionType compression;
    RequestFlags flags;

    bool operator==(const Request& o) const
    {
        return query == o.query && compression == o.compression && flags == o.flags;
    }

    static u32 Hash(const Request& r);
};

// MUST be allocated with new when you use this!
struct StoredRequest : public Refcounted
{
    StoredRequest() : expiryTime(0) {}
    virtual ~StoredRequest() {}

    u64 expiryTime;
    std::vector<char> body;
};

// TODO: remove requirement for external buffer to reduce copying
class StoredRequestWriteStream : public BufferedWriteStream
{
public:
    StoredRequestWriteStream(StoredRequest *req, char* buf, size_t bufsize); // mg_connection
private:
    static size_t _Write(const void* src, size_t bytes, BufferedWriteStream* self);
    StoredRequest* const _req;
};
