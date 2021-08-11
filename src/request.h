#pragma once

#include <string>
#include <vector>
#include "refcounted.h"
#include "jsonstreamwrapper.h"

class Request
{
public:
    Request()
        : compression(0), flags(0) {}

    Request(const char* query, unsigned compression, unsigned flags)
        : compression(compression), flags(flags) {}

    std::string query;
    unsigned compression;
    unsigned flags;

    bool operator==(const Request& o) const
    {
        return query == o.query && compression == o.compression && flags == o.flags;
    }

    static u32 Hash(const Request& r);
};

// MUST be allocated with new when you use this!
struct StoredRequest : public Refcounted
{
    u64 expiryTime;
    std::vector<char> body;
};

class StoredRequestWriteStream : public BufferedWriteStream
{
public:
    StoredRequestWriteStream(StoredRequest *req, char* buf, size_t bufsize); // mg_connection
private:
    static size_t _Write(const void* src, size_t bytes, BufferedWriteStream* self);
    StoredRequest* const _req;
};
