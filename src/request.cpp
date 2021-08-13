#include <assert.h>
#include "request.h"
#include "util.h"

#include "civetweb/civetweb.h"

static CompressionType parseEncoding(const char *enc)
{
    CompressionType c = COMPR_NONE;

    if(strstr(enc, "deflate"))
        c = COMPR_DEFLATE;

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
