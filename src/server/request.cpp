#include <assert.h>
#include <string.h>
#include "request.h"
#include "util.h"
#include "variant.h"
#include "json_in.h"

#include "civetweb/civetweb.h"

struct RequestTypeLUT
{
    RequestType type;
    const char *str;
};

static const RequestTypeLUT requestTypeLUT[] =
{
    { RQ_GET,     "GET" },
    { RQ_POST,    "POST" },
    { RQ_HEAD,    "HEAD" },
    { RQ_OPTIONS, "OPTIONS" },
    { RQ_PUT,     "PUT" },
    { RQ_DELETE,  "DELETE" },
};

static const RequestType getRequestType(const char *s)
{
    for(size_t i = 0; i < Countof(requestTypeLUT); ++i)
        if(!stricmp(s, requestTypeLUT[i].str))
            return requestTypeLUT[i].type;
    return RQ_UNKNOWN;
}


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
        {
            if (atoi(hd[i].value))
                r.flags |= RQF_PRETTY;
        }
        else if(!strcmp(hd[i].name, "fmt"))
        {
            for(size_t i = 0; i < Countof(RequestFormatName); ++i)
                if(RequestFormatName[i] && !strcmp(RequestFormatName[i], hd[i].value))
                    r.fmt = (RequestFormat)i;
        }
        else if(!strcmp(hd[i].name, "json"))
            r.fmt = RQFMT_JSON;
    }
    return true;
}

// parse ?a=bla&b=0 to a Var
static int importQueryStrVars(VarRef v, const char* vars)
{
    assert(vars);
    char tmp[8 * 1024];
    const size_t tocopy = strlen(vars) + 1; // ensure to always include \0
    if (tocopy > sizeof(tmp))
        return -99;
    memcpy(tmp, vars, tocopy);
    mg_header hd[MG_MAX_HEADERS];
    const int num = mg_split_form_urlencoded(tmp, hd, MG_MAX_HEADERS);
    if (num < 0)
        return num;
    for (int i = 0; i < num; ++i)
        v[hd[i].name] = hd[i].value ? hd[i].value : "";
    return num;
}

static int field_found(const char* key,
    const char* filename,
    char* path,
    size_t pathlen,
    void* user_data)
{
    return MG_FORM_FIELD_STORAGE_GET;
}

static int field_get(const char* key,
    const char* value,
    size_t valuelen,
    void* user_data)
{
    VarRef& v = *(VarRef*)user_data;
    v[key].setStr(value, valuelen);

    return MG_FORM_FIELD_HANDLE_NEXT;
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
        const char *k = info->http_headers[i].name;
        const char *v = info->http_headers[i].value;
        if(!mg_strcasecmp("Accept-Encoding", k))
        {
            CompressionType c = parseEncoding(v);
            if(c > this->compression) // preference by value
                this->compression = c;
        }
        if (!mg_strcasecmp("Authorization", k))
        {
            authorization = v;
        }
    }

    this->type = getRequestType(info->request_method);

    return true;
}

u32 Request::Hash(const Request& r)
{
    u32 hash = strhash(r.query.c_str());
    hash ^= (r.compression << 2) ^ r.flags ^ (r.fmt << 7) ^ (r.type << 8);
    return hash;
}

int Request::ReadQueryVars(VarRef dst, const mg_request_info* info)
{
    const char* vq = info->query_string;
    if(!vq)
        return -1;
    return importQueryStrVars(dst, vq);
}

int Request::ReadJsonBodyVars(VarRef dst, mg_connection* conn, bool ignoreMIME, bool acceptNotMap, size_t maxsize)
{
    if(!ignoreMIME)
    {
        const char* content_type = mg_get_header(conn, "Content-Type");
        if(!content_type)
            return -1;

        if( mg_strncasecmp(content_type, "application/json", 16))
            return -2;
    }

    // TODO: Use a BufferedReadStream to load successively rather than slurping up the entire thing first
    std::vector<char> rd;
    while (maxsize)
    {
        char buf[1024];
        int done = mg_read(conn, buf, sizeof(buf)); // TODO: handle lack of data gracefully
        if (done > 0)
        {
            maxsize -= done;
            rd.insert(rd.end(), &buf[0], &buf[done]);
        }
        else if(!done) // Connection closed? Get out.
            break;
    }
    if (!loadJsonDestructive(dst, rd.data(), rd.size()))
        return -3;

    if (!acceptNotMap && dst.type() != Var::TYPE_MAP)
        return -4;

    return (int)dst.size();
}

int Request::ReadFormDataVars(VarRef dst, mg_connection* conn)
{
    mg_form_data_handler handleForm = { field_found, field_get, NULL, &dst };
    return mg_handle_form_request(conn, &handleForm);
}

int Request::AutoReadVars(VarRef dst, mg_connection* conn)
{
    const mg_request_info* info = mg_get_request_info(conn);

    int json = ReadJsonBodyVars(dst, conn, false, false);
    if(json <= -3)
        return json; // if it's json it should at least be valid and eval to a map
    int fd = ReadFormDataVars(dst, conn);
    int q = ReadQueryVars(dst, info);
    if(json < 0 && fd < 0 && q < 0)
        return -1; // everything failed

    return (int)dst.size();
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

