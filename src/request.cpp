#include "request.h"
#include "util.h"

u32 Request::Hash(const Request& r)
{
    u32 hash = strhash(r.query.c_str());
    hash ^= (r.compression << 2) ^ r.flags;
    return hash;
}

StoredRequestWriteStream::StoredRequestWriteStream(StoredRequest* req, char* buf, size_t bufsize)
    : BufferedWriteStream(NULL, _Write, buf, bufsize)
    , _req(req)
{
}

size_t StoredRequestWriteStream::_Write(const void* src, size_t bytes, BufferedWriteStream* self)
{
    StoredRequestWriteStream* me = static_cast<StoredRequestWriteStream*>(self);
    std::vector<char>& v = me->_req->body;

    size_t cur = v.size();
    size_t req = cur + bytes;
    if(v.capacity() < req)
        v.reserve(req * 2);

    v.resize(req);
    memcpy(&v[cur], src, bytes);

    return bytes;
}
