#include "luafuncs.h"
#include <lua/lua.hpp>

#include <future>
#include "treemem.h"
#include "util.h"
#include "json_out.h"
#include "json_in.h"
#include "civetweb/civetweb.h"
#include <rapidjson/reader.h>

enum
{
    HTTP_PORT = 80
};


const char* str(lua_State* L, int idx /* = 1 */)
{
    if (lua_type(L, idx) != LUA_TSTRING)
        luaL_error(L, "parameter %d: expected string");
    return lua_tostring(L, idx);
}

PoolStr strL(lua_State* L, int idx /* = 1 */)
{
    if (lua_type(L, idx) != LUA_TSTRING)
        luaL_error(L, "parameter %d: expected string");
    PoolStr ps;
    ps.s = lua_tolstring(L, idx, &ps.len);
    return ps;
}

u64 checkU64(lua_State* L, int idx /* = 1 */)
{
    int ok = 0;
    lua_Integer i = lua_tointegerx(L, idx, &ok);
    if (!ok || i < 0)
        luaL_error(L, "expected size (an integer >= 0)");
    return static_cast<u64>(i);
}

bool getBool(lua_State* L, int idx /* = 1 */)
{
    int t = lua_type(L, idx);
    switch (t)
    {
    case LUA_TNIL:
    case LUA_TNONE:
        return false;
    case LUA_TBOOLEAN:
        return lua_toboolean(L, idx);
    case LUA_TNUMBER:
        return !!lua_tointeger(L, idx);
    }
    return luaL_typeerror(L, idx, "expected bool");
}

size_t Sz(lua_State * L, int idx /* = 1 */)
{
    return static_cast<size_t>(checkU64(L, idx));
}


typedef rapidjson::Writer<rapidjson::StringBuffer> Wr;

static bool emitJson(Wr& w, lua_State* L, int idx, bool strict)
{
    idx = lua_absindex(L, idx);
    if (idx > 64)
        luaL_error(L, "nesting too deep");

    switch (lua_type(L, idx))
    {
    case LUA_TSTRING: return w.String(lua_tostring(L, idx));
    case LUA_TNUMBER:
        if (lua_isinteger(L, idx))
            return w.Int64(lua_tointeger(L, idx));
        else
            return w.Double(lua_tonumber(L, idx));
        return true;
    case LUA_TNIL:
    case LUA_TNONE:
        return w.Null();
    case LUA_TBOOLEAN:
        return w.Bool(lua_toboolean(L, idx));
    case LUA_TTABLE:
    {
        const size_t n = lua_rawlen(L, idx);
        if (n) // array?
        {
            w.StartArray();
            for (size_t i = 0; i < n; ++i)
            {
                lua_rawgeti(L, idx, i);
                emitJson(w, L, -1, strict);
            }
            return w.EndArray();
        }

        // object/map.
        w.StartObject();

        lua_pushnil(L);  /* first key */
        // [_G][nil]
        while (lua_next(L, idx) != 0)
        {
            // [_G][k][v]
            if (lua_type(L, -2) == LUA_TSTRING) // only string keys supported in JSON
            {
                size_t len;
                const char* k = lua_tolstring(L, -2, &len);
                if (!w.String(k, (rapidjson::SizeType)len, true))
                    return false;
                if (!emitJson(w, L, -1, strict))
                {
                    if (strict)
                        return false;
                    else if (!w.Null())
                        return false;
                }
            }
            else if (strict)
                return false;
            lua_pop(L, 1);
            // [_G][k]
        }
        return w.EndObject();
    }
    }
    return false;
}

static int api_json(lua_State* L)
{
    // TODO: might want to use an adapter for luaL_Buffer instead of this if this ever becomes a perf problem
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> wr(sb);
    bool strict = getBool(L, 2);
    if (emitJson(wr, L, 1, strict))
    {
        wr.Flush();
        lua_pushlstring(L, sb.GetString(), sb.GetLength());
        return 1;
    }

    return 0;
}


struct LuaJsonLoader
{
    enum
    {
        ParseFlags = 0
        | rapidjson::kParseValidateEncodingFlag
        | rapidjson::kParseIterativeFlag
        | rapidjson::kParseFullPrecisionFlag
        | rapidjson::kParseCommentsFlag
        | rapidjson::kParseNanAndInfFlag
        | rapidjson::kParseEscapedApostropheFlag
        | rapidjson::kParseTrailingCommasFlag
    };

    struct Frame
    {
        Frame(bool a) : isobj(a), idx(0) {}
        const bool isobj;
        lua_Integer idx;
    };

    bool _doadd()
    {
        if (!frames.empty())
        {
            Frame& f = frames.back();
            if (f.isobj)
            {
                assert(lua_type(L, -3) == LUA_TTABLE);
                assert(lua_type(L, -2) == LUA_TSTRING); // key must be string
                lua_rawset(L, -3); // key and value are in slots -2 and -1, respectively
            }
            else
            {
                assert(lua_type(L, -2) == LUA_TTABLE);
                lua_rawseti(L, -2, ++f.idx);
            }
            assert(lua_type(L, -1) == LUA_TTABLE); // current obj/array must be on top now
        }
        // else it's a single value, just leave that on the stack
        return true;
    }

    bool _pushframe(bool isobj)
    {
        frames.emplace_back(isobj);
        lua_newtable(L);
        return true;
    }

    bool _popframe(bool isobj)
    {
        assert(lua_type(L, -1) == LUA_TTABLE);
        assert(frames.back().isobj == isobj);
        frames.pop_back();
        return _doadd();
    }

    // ---- begin rapidjson stuff -----

    bool Null() { lua_pushnil(L); return _doadd(); }
    bool Bool(bool b) { lua_pushboolean(L, b); return _doadd(); }
    bool Int(int i) { lua_pushinteger(L, i); return _doadd(); }
    bool Uint(unsigned u) { lua_pushinteger(L, u); return _doadd(); }
    bool Int64(int64_t i) { lua_pushinteger(L, i); return _doadd(); }
    bool Uint64(uint64_t u) { lua_pushinteger(L, u); return _doadd(); }  // FIXME: overflow?
    bool Double(double d) { lua_pushnumber(L, d); return _doadd(); }
    bool RawNumber(const char* str, size_t length, bool copy)
    { // only for when kParseNumbersAsStringsFlag is set (which we don't)
        assert(false); // not called
        return false;
    }
    bool String(const char* str, size_t length, bool copy)
    {
        lua_pushlstring(L, str, length);
        return _doadd();
    }
    bool StartObject()
    {
        return _pushframe(true);
    }
    bool Key(const char* str, size_t length, bool copy)
    {
        lua_pushlstring(L, str, length); // value comes next
        return true;
    }
    bool EndObject(size_t memberCount)
    {
        return _popframe(true);
    }
    bool StartArray()
    {
        return _pushframe(false);
    }
    bool EndArray(size_t elementCount)
    {
        return _popframe(false);
    }

    // ---- end rapidjson stuff -----

    LuaJsonLoader(lua_State* L) : L(L) {}

    template<typename STREAM>
    bool parse(STREAM& stream)
    {
        rapidjson::Reader rd;
        return rd.Parse<ParseFlags>(stream, *this);
    }

private:
    lua_State* const L;
    std::vector<Frame> frames;
};

static int api_loadjson(lua_State* L)
{
    LuaJsonLoader ld(L);
    const char* s = str(L);
    rapidjson::StringStream ss(s);
    if (!ld.parse(ss))
    {
        lua_pushnil(L);
        lua_pushstring(L, "parse error");
        return 2;
    }
    return 1;
}

template<typename T>
static int pushObj(lua_State* L, T* obj, const char* mtname)
{
    void* ud = lua_newuserdatauv(L, sizeof(T*), 0);
    *(T**)ud = obj;

    // assign metatable to make sure __gc is called if necessary
    int mt = luaL_getmetatable(L, mtname);
    assert(mt == LUA_TTABLE);
    lua_setmetatable(L, -2); // pops mt

    return 1; // the userdata still on the stack
}

template<typename T>
static T*& getObjPtrRef(lua_State* L, const char* mtname, int idx = 1)
{
    void* ud = luaL_checkudata(L, 1, mtname);
    return *(T**)ud;
}

template<typename T>
static T* getObj(lua_State* L, const char* mtname, int idx = 1)
{
    T* obj = getObjPtrRef<T>(L, mtname, idx);
    if (!obj)
        luaL_error(L, "object is invalid or was already deleted");
    return obj;
}

static int pushConn(lua_State* L, mg_connection* conn)
{
    return pushObj(L, conn, "mg_connection");
}
static mg_connection* getConn(lua_State* L, int idx = 1)
{
    return getObj<mg_connection>(L, "mg_connection", idx);
}
static mg_connection*& getConnRef(lua_State* L, int idx = 1)
{
    return getObjPtrRef<mg_connection>(L, "mg_connection", idx);
}

// Workaround to connect to a HTTP resource in background
// so we don't have to wait until the socket is connected or incoming headers are parsed
struct BackgroundHttpRequest
{
    BackgroundHttpRequest(const char* req, size_t reqsize, const char* host, unsigned port, bool ssl, int timeout)
        : request(req, req + reqsize), host(host), port(port), timeout(timeout), ssl(ssl)
        , conn(NULL), responseFut(responseProm.get_future())
    {
        std::thread(_Run_Th, this).detach();
    }

    ~BackgroundHttpRequest()
    {
        responseFut.wait();
        mg_close_connection(conn); // handles NULL gracefully
    }

    int pushResult(lua_State* L)
    {
        if (responseFut.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
            return lua_yieldk(L, 0, (lua_KContext)this, pushResultK);

        int resp = responseFut.get();
        if (!conn)
        {
            lua_pushnil(L);
            lua_pushstring(L, errbuf);
            return 2;
        }

        const mg_response_info* ri = mg_get_response_info(conn);
        pushConn(L, conn);
        lua_pushinteger(L, ri->status_code);
        lua_pushstring(L, ri->status_text);
        lua_pushinteger(L, ri->content_length);
        lua_createtable(L, 0, ri->num_headers);
        for (int i = 0; i < ri->num_headers; ++i)
        {
            lua_pushstring(L, ri->http_headers[i].value);
            lua_setfield(L, -2, ri->http_headers[i].name);
        }
        strcpy(errbuf, "already retrieved result");
        conn = NULL;
        return 5; // conn, status, text, contentLen, {headers}
    }

private:

    void _run()
    {
        int resp = -1;
        if (mg_connection* c = mg_connect_client(host.c_str(), port, ssl, errbuf, sizeof(errbuf)))
        {
            conn = c;
            mg_write(c, request.data(), request.size());
            resp = mg_get_response(c, errbuf, sizeof(errbuf), (int)timeout);
        }
        responseProm.set_value(resp);
    }

    static void _Run_Th(BackgroundHttpRequest* self)
    {
        self->_run();
    }
    static int pushResultK(lua_State* L, int status, lua_KContext ctx)
    {
        BackgroundHttpRequest* self = (BackgroundHttpRequest*)ctx;
        return self->pushResult(L);
    }

    const std::vector<char> request;
    const std::string host;
    const unsigned port;
    const int timeout;
    const bool ssl;
    mg_connection* conn;
    std::promise<int> responseProm;
    std::shared_future<int> responseFut;
    char errbuf[256];
};

static int api_httprequest(lua_State* L)
{
    PoolStr req = strL(L, 1);
    const char* host = str(L, 2);
    const int port = (int)luaL_optinteger(L, 3, HTTP_PORT);
    bool ssl = getBool(L, 4);
    int timeout = (int)luaL_optinteger(L, 5, -1);

    BackgroundHttpRequest* bg = new BackgroundHttpRequest(req.s, req.len, host, port, ssl, timeout);
    pushObj(L, bg, "BackgroundHttpRequest"); // let the Lua GC take care of it from now on
    return bg->pushResult(L);
}

static int api_BackgroundHttpRequest__gc(lua_State* L)
{
    BackgroundHttpRequest*& bg = getObjPtrRef<BackgroundHttpRequest>(L, "BackgroundHttpRequest");
    delete bg;
    bg = NULL;
    return 0;
}

static int api_httpopen(lua_State* L)
{
    const char* host = str(L, 1);
    const int port = (int)luaL_optinteger(L, 2, HTTP_PORT);
    bool ssl = getBool(L, 3);

    char errbuf[256];
    mg_connection* conn = mg_connect_client(host, port, ssl, errbuf, sizeof(errbuf));
    if (!conn)
    {
        lua_pushnil(L);
        lua_pushstring(L, errbuf);
        return 2;
    }

    return pushConn(L, conn);
}

static int api_mg_connection_close(lua_State* L)
{
    if (mg_connection*& connref = getConnRef(L))
    {
        mg_close_connection(connref);
        connref = NULL;
    }
    return 0;
}

static int api_mg_connection__gc(lua_State* L)
{
    return api_mg_connection_close(L);
}

static int api_mg_connection_write(lua_State* L)
{
    mg_connection* conn = getConn(L);
    size_t n;
    const char* s = luaL_checklstring(L, 2, &n);
    int sent = mg_write(conn, s, n);
    lua_pushinteger(L, sent);
    return 1;
}

static int api_mg_connection_readResponse(lua_State* L)
{
    mg_connection* conn = getConn(L);
    lua_Integer timeout = luaL_optinteger(L, 2, -1);
    // TODO: this should be in another thread so we don't block?
    char err[256];
    int resp = mg_get_response(conn, err, sizeof(err), (int)timeout);
    if (resp < 0)
    {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    const mg_response_info* ri = mg_get_response_info(conn);
    lua_pushinteger(L, ri->status_code);
    lua_pushstring(L, ri->status_text);
    lua_pushinteger(L, ri->content_length);
    lua_createtable(L, 0, ri->num_headers);
    for (int i = 0; i < ri->num_headers; ++i)
    {
        lua_pushstring(L, ri->http_headers[i].value);
        lua_setfield(L, -2, ri->http_headers[i].name);
    }
    return 4; // status, text, contentLen, {headers}
}

// civetweb sets each socket as non-blocking, so a return of -1 is normal
static int api_mg_connection_read(lua_State* L)
{
    mg_connection* conn = getConn(L);
    int maxrd = (int)luaL_optinteger(L, 2, -1);

    char buf[1024];
    int bufrd = sizeof(buf) < maxrd ? sizeof(buf) : maxrd;
    int rd = mg_read(conn, buf, bufrd);
    if (!rd)
    {
        lua_pushliteral(L, ""); // no data
        return 1;
    }
    if (rd < 0)
    {
        lua_pushnil(L);
        lua_pushliteral(L, "error");
        return 2;
    }
    if (rd < sizeof(buf)) // small read, just return what we have
    {
        lua_pushlstring(L, buf, rd);
        return 1;
    }
    // large read, concat pieces to large buffer
    maxrd -= rd;
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    luaL_addlstring(&b, buf, rd);

    while (maxrd)
    {
        int tord = std::min(bufrd, maxrd);
        rd = mg_read(conn, buf, tord);
        if (rd < 0)
            break; // b known to contain data; handle partial buffer
        luaL_addlstring(&b, buf, rd);
        maxrd -= rd;
        if (tord != rd) // drained everything that was available for now
            break;
    }
    luaL_pushresult(&b);
    return 1;
}

static int api_base64enc(lua_State* L)
{
    size_t len;
    const char* s = luaL_checklstring(L, 1, &len);
    std::vector<char> enc(base64size(len));
    size_t enclen = base64enc(enc.data(), (unsigned char*)s, len);
    lua_pushlstring(L, enc.data(), enclen); // does not include the \0
    return 1;
}

static int api_base64dec(lua_State* L)
{
    size_t len;
    const char* s = luaL_checklstring(L, 1, &len);
    std::vector<char> dec(len);
    size_t declen = 0;
    if (len && !base64dec(dec.data(), &declen, (unsigned char*)s, len))
        luaL_error(L, "base64dec() failed");
    lua_pushlstring(L, dec.data(), declen);
    return 1;
}


static const luaL_Reg reg[] =
{
    { "json",      api_json },
    { "loadjson",  api_loadjson },
    { "httpopen",  api_httpopen },
    { "base64enc", api_base64enc },
    { "base64dec", api_base64dec },
    { "httprequest", api_httprequest },
    { NULL,        NULL }
};

static const luaL_Reg mg_conn_reg[] =
{
    { "__gc",         api_mg_connection__gc },
    { "readResponse", api_mg_connection_readResponse },
    { "read",         api_mg_connection_read },
    { "write",        api_mg_connection_write },
    { "close",        api_mg_connection_close },
    { NULL,           NULL }
};

static const luaL_Reg BackgroundHttpRequest_reg[] =
{
    { "__gc", api_BackgroundHttpRequest__gc },
    { NULL,   NULL }
};

static void makeMT(lua_State* L, const luaL_Reg* reg, const char* name)
{
    luaL_newmetatable(L, name);
    luaL_setfuncs(L, reg, 0);
    lua_setfield(L, -1, "__index"); // set and pop self
}

void luafunc_register(lua_State* L)
{
    lua_pushglobaltable(L);
    luaL_setfuncs(L, reg, 0);
    lua_pop(L, 1);

    makeMT(L, mg_conn_reg, "mg_connection");
    makeMT(L, BackgroundHttpRequest_reg, "BackgroundHttpRequest");
}



static void luaImportVarArray(lua_State* L, VarCRef ref)
{
    const Var* a = ref.v->array();
    const size_t n = ref.size();
    lua_createtable(L, (int)n, 0);
    const int top = lua_gettop(L);
    for (size_t i = 0; i < n; ++i)
    {
        luaImportVar(L, VarCRef(ref.mem, &a[i]));
        lua_rawseti(L, -2, i + 1);
    }
    assert(lua_gettop(L) == top);
}

static void luaImportVarMap(lua_State* L, VarCRef ref)
{
    const Var::Map* m = ref.v->map();
    lua_createtable(L, 0, (int)m->size());
    const int top = lua_gettop(L);
    for (Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
    {
        PoolStr ps = ref.mem->getSL(it.key());
        lua_pushlstring(L, ps.s, ps.len);
        luaImportVar(L, VarCRef(ref.mem, &it.value()));
        lua_rawset(L, top);
    }
    assert(lua_gettop(L) == top);
}

void luaImportVar(lua_State* L, VarCRef ref)
{
    switch (ref.type())
    {
        case Var::TYPE_NULL: lua_pushnil(L); break;
        case Var::TYPE_BOOL: lua_pushboolean(L, ref.asBool()); break;
        case Var::TYPE_STRING: { PoolStr ps = ref.asString(); lua_pushlstring(L, ps.s, ps.len); } break;
        case Var::TYPE_FLOAT: lua_pushnumber(L, *ref.asFloat()); break;
        case Var::TYPE_INT: lua_pushinteger(L, *ref.asInt()); break;
        case Var::TYPE_UINT: lua_pushinteger(L, *ref.asUint()); break; // FIXME: check value range?
        case Var::TYPE_ARRAY: luaImportVarArray(L, ref); break;
        case Var::TYPE_MAP: luaImportVarMap(L, ref); break;
        case Var::TYPE_PTR: lua_pushlightuserdata(L, ref.asPtr()); break;
        default: lua_pushnil(L); break; // can't do anything better
    }
}
