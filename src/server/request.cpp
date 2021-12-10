#include <assert.h>
#include <string.h>
#include "request.h"
#include "util.h"

#include "civetweb/civetweb.h"


static CompressionType parseEncoding(const char *enc)
{
    CompressionType c = COMPR_NONE;

    // first elem is NULL, skip that
    for(unsigned i = COMPR_NONE + 1; !c && i < Countof(CompressionTypeName); ++i)
        if(strstr(enc, CompressionTypeName[i]))
            c = (CompressionType)i;

    return c;
}

static bool parseAndApplyVars(Request& r, const char *vars)
{
    char tmp[8 * 1024];
    const size_t tocopy = strlen(vars) + 1; // ensure to always include \0
    if (tocopy > sizeof(tmp))
        return false;
    memcpy(tmp, vars, tocopy);
    mg_header hd[MG_MAX_HEADERS];
    const int num = mg_split_form_urlencoded(tmp, hd, MG_MAX_HEADERS);
    if (num < 0)
        return false;
    for (int i = 0; i < num; ++i)
    {
        if (!strcmp(hd[i].name, "pretty"))
            if (atoi(hd[i].value))
                r.flags |= RQF_PRETTY;
    }
    return true;
}

bool Request::parse(const mg_request_info* info, size_t skipFromQuery)
{
    if(!info->local_uri)
        return false;

    // if this fires then the handler was called even though it shouldn't,
    // or the request handler wasn't set up with the right skip length
    assert(strlen(info->local_uri) >= skipFromQuery);

    this->query = info->local_uri + skipFromQuery;

    const char* vars = info->query_string;
    if(vars && *vars)
        if(!parseAndApplyVars(*this, vars))
            return false;

    const int n = info->num_headers;
    for(int i = 0; i < n; ++i)
    {
        if(!mg_strcasecmp("Accept-Encoding", info->http_headers[i].name))
        {
            CompressionType c = parseEncoding(info->http_headers[i].value);
            if(c > this->compression) // preference by value
                this->compression = c;
        }
    }

    return true;
}

u32 Request::Hash(const Request& r)
{
    u32 hash = strhash(r.query.c_str());
    hash ^= (r.compression << 2) ^ r.flags;
    return hash;
}

StoredReplyWriteStream::StoredReplyWriteStream(StoredReply* req, char* buf, size_t bufsize)
    : BufferedWriteStream(NULL, _Write, buf, bufsize)
    , _req(req)
{
}

size_t StoredReplyWriteStream::_Write(const void* src, size_t bytes, BufferedWriteStream* self)
{
    StoredReplyWriteStream* me = static_cast<StoredReplyWriteStream*>(self);
    std::vector<char>& v = me->_req->data;

    size_t cur = v.size();
    size_t req = cur + bytes;
    if(v.capacity() < req)
        v.reserve(req * 2);

    v.resize(req);
    memcpy(&v[cur], src, bytes);

    return bytes;
}

bool StoredReply::spliceHeader(const char* hdr1, size_t sz1, const char* hdr2, size_t sz2)
{
    size_t total = sz1 + sz2;
    // this is really bad if this happened, so we make very sure this can't stomp the buffer
    assert(total < reservedHeaderSpace);
    if(total >= reservedHeaderSpace)
        return false;

    size_t offs = reservedHeaderSpace - total;
    memcpy(data.data() + offs, hdr1, sz1);
    memcpy(data.data() + offs + sz1, hdr2, sz2);
    hdrstart = offs;
    return true;
}
