#include <lua/lua.hpp>
#include <sstream>
#include "sisclient.h"
#include "treemem.h"
#include "util.h"
#include "json_out.h"
#include "json_in.h"
#include "civetweb/civetweb.h"
#include <rapidjson/reader.h>

enum
{
    BufferSize = 256,  // bytes
};

static void setclient(lua_State *L, SISClient *cl)
{
    lua_pushliteral(L, ".client");
    lua_pushlightuserdata(L, cl);
    lua_rawset(L, LUA_REGISTRYINDEX);
}
static SISClient& client(lua_State *L)
{
    lua_pushliteral(L, ".client");
    lua_rawget(L, LUA_REGISTRYINDEX);
    void *p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    if(!p)
        luaL_error(L, ".client is NULL");
    return *static_cast<SISClient*>(p);
}

static const char *str(lua_State *L, int idx = 1)
{
    if(lua_type(L, idx) != LUA_TSTRING)
        luaL_error(L, "parameter %d: expected string");
    return lua_tostring(L, idx);
}

static PoolStr strL(lua_State* L, int idx = 1)
{
    if (lua_type(L, idx) != LUA_TSTRING)
        luaL_error(L, "parameter %d: expected string");
    PoolStr ps;
    ps.s = lua_tolstring(L, idx, &ps.len);
    return ps;
}

static u64 checkU64(lua_State *L, int idx = 1)
{
    int ok = 0;
    lua_Integer i = lua_tointegerx(L, idx, &ok);
    if(!ok || i < 0)
        luaL_error(L, "expected size (an integer >= 0)");
    return static_cast<u64>(i);
}

static bool getBool(lua_State *L, int idx = 1)
{
    int t = lua_type(L, idx);
    switch(t)
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


static size_t Sz(lua_State *L, int idx = 1)
{
    return static_cast<size_t>(checkU64(L, idx));
}

// (str), yieldable
static int expectK(lua_State* L, int status, lua_KContext ctx);
static void expect(lua_State *L, SISClient& client, PoolStr ps, size_t offset)
{
    assert(offset < ps.len);
    char buf[BufferSize];
    ps.s += offset;
    ps.len -= offset;
    while (ps.len)
    {
        int rd = client.readInput(buf, std::min(sizeof(buf)-1, ps.len)); // keep space for terminating nullbyte
        if (rd < 0)
            luaL_error(L, "client.readInput() failed with error %d", rd);
        else if (!rd)
        {
            lua_yieldk(L, 0, offset, expectK);
            continue;
        }
        if (memcmp(buf, ps.s, rd))
        {
            buf[rd] = 0;
            luaL_error(L, "expect [%s] failed, got (%d bytes):\n%s\n", ps.s, rd, buf);
        }
        offset += rd;
        ps.s += rd;
        ps.len -= rd;
    }
}
static int expectK(lua_State* L, int status, lua_KContext ctx)
{
    expect(L, client(L), strL(L), (size_t)ctx);
    return 0;
}
static int api_expect(lua_State* L)
{
    //return expectK(L, lua_pcallk(L, 1, 0, 0, 0, expectK), 0);
    return expectK(L, LUA_OK, 0);
}

// (int), yieldable
static int skipK(lua_State* L, int status, lua_KContext ctx);
static void skip(lua_State* L, SISClient& client, size_t remain)
{
    while (remain)
    {
        const size_t avail = client.availInput();
        if (avail)
        {
            const size_t skip = std::min(avail, remain);
            remain -= skip;
            client.advanceInput(skip);
        }
        else
            lua_yieldk(L, 0, remain, skipK);
    }
}
static int skipK(lua_State* L, int status, lua_KContext ctx)
{
    skip(L, client(L), (size_t)ctx);
    return 0;
};
static int api_skip(lua_State* L)
{
    size_t n = Sz(L);
    //return skipK(L, lua_pcallk(L, 1, 0, 0, n, skipK), n);
    return skipK(L, LUA_OK, n);
}

// no params, not yieldable
static void skipall(SISClient& client)
{
    size_t n = client.availInput();
    client.advanceInput(n);
}
static int api_skipall(lua_State* L)
{
    skipall(client(L));
    return 0;
}

// (int), yieldable
static int needK(lua_State* L, int status, lua_KContext ctx);
static void need(lua_State *L, const SISClient& client, size_t n)
{
    while (client.availInput() < n)
        lua_yieldk(L, 0, n, needK);
}
static int needK(lua_State* L, int status, lua_KContext ctx)
{
    need(L, client(L), (size_t)ctx);
    return 0;
}
static int api_need(lua_State* L)
{
    size_t n = Sz(L);
    //return needK(L, lua_pcallk(L, 1, 0, 0, n, needK), n);
    return needK(L, LUA_OK, n);
}

// (str), yieldable
static int sendK(lua_State* L, int status, lua_KContext ctx);
static void send(lua_State *L, SISClient& client, PoolStr ps, size_t offset)
{
    assert(offset <= ps.len);
    ps.s += offset;
    ps.len -= offset;
    for (;;)
    {
        int sent = client.sendsome(ps.s, ps.len);
        if (sent < 0)
            luaL_error(L, "client.sendsome() failed with error %d", sent);

        if ((size_t)sent == ps.len)
            return;

        offset += sent;
        ps.s += sent;
        ps.len -= sent;
        lua_yieldk(L, 0, offset, sendK);
    }
}
static int sendK(lua_State* L, int status, lua_KContext ctx)
{
    send(L, client(L), strL(L), (size_t)ctx);
    return 0;
}
static int api_send(lua_State* L)
{
    //return sendK(L, lua_pcallk(L, 1, 0, 0, 0, sendK), 0);
    return sendK(L, LUA_OK, 0);
}

// yieldable
static int readlineK(lua_State* L, int status, lua_KContext ctx);
static int readline(lua_State *L, SISClient& client, bool allownil)
{
    std::ostringstream os;
    const size_t avail = client.availInput();
    if(avail)
    {
        const char * const in = client.getInputPtr();
        size_t i = 0;
        char c = 0;
        bool out = false;
        while(i < avail)
        {
            c = in[i++];
            out = !c || c == '\n' || c == '\r';
            if(out)
                break;
            os << c;
        }
        // accept CR and CRLF
        if(out)
        {
            if(i < avail && c == '\r' && in[i] == '\n')
                ++i;
            client.advanceInput(i);
            std::string s = os.str();
            lua_pushlstring(L, s.c_str(), s.length());
            return 1;
        }
        else if(allownil)
            return 0;
    }
    else if (allownil)
        return 0;
    return lua_yieldk(L, 0, 0, readlineK);
}
static int readlineK(lua_State* L, int status, lua_KContext ctx)
{
    return readline(L, client(L), getBool(L, 1));
}
static int api_readline(lua_State* L)
{
    return readlineK(L, LUA_OK, 0);
}

// (int), yieldable
static int readnK(lua_State* L, int status, lua_KContext ctx);
static std::string readn(lua_State* L, SISClient& client, size_t n)
{
    std::ostringstream os;
    while(n)
    {
        const size_t avail = client.availInput();
        if (avail)
        {
            const char* const in = client.getInputPtr();
            const size_t want = std::min(avail, n);
            os.write(in, want);
            client.advanceInput(want);
            n -= want;
        }
        else
            lua_yieldk(L, 0, n, readnK);
    }
    return os.str();
}
static int readnK(lua_State* L, int status, lua_KContext ctx)
{
    std::string s = readn(L, client(L), (size_t)ctx);
    lua_pushlstring(L, s.c_str(), s.length());
    return 1;
}
static int api_readn(lua_State* L)
{
    size_t n = Sz(L);
    //return readnK(L, lua_pcallk(L, 1, 0, 0, n, readnK), n);
    return readnK(L, LUA_OK, n);
}

static int peek(lua_State *L, SISClient& cl)
{
    const char *buf = cl.getInputPtr();
    size_t n = cl.availInput();
    lua_pushlstring(L, buf, n);
    lua_pushinteger(L, n);
    return 2;
}
static int api_peek(lua_State *L)
{
    return peek(L, client(L));
}

static int api_timeout(lua_State *L)
{
    u64 ms = 0;
    const int ty = lua_type(L, 1);
    if(ty == LUA_TNUMBER)
    {
        lua_Integer i = lua_tointeger(L, 1);
        if(i < 0)
            luaL_error(L, "ms must be >= 0");
        ms = i;
    }
    else if(!strToDurationMS_Safe(&ms, lua_tostring(L, 1)))
        luaL_argerror(L, 1, "expected time in ms or duration as string");

    client(L).setTimeout(ms);
    return 0;
}


typedef rapidjson::Writer<rapidjson::StringBuffer> Wr;

static bool emitJson(Wr& w, lua_State *L, int idx, bool strict)
{
    idx = lua_absindex(L, idx);
    if(idx > 64)
        luaL_error(L, "nesting too deep");

    switch(lua_type(L, idx))
    {
        case LUA_TSTRING: return w.String(lua_tostring(L, idx));
        case LUA_TNUMBER:
            if(lua_isinteger(L, idx))
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
            if(n) // array?
            {
                w.StartArray();
                for(size_t i = 0; i < n; ++i)
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
                    const char *k = lua_tolstring(L, -2, &len);
                    if(!w.String(k, len, true))
                        return false;
                    if(!emitJson(w, L, -1, strict))
                    {
                        if(strict)
                            return false;
                        else if(!w.Null())
                            return false;
                    }
                }
                else if(strict)
                    return false;
                lua_pop(L, 1);
                // [_G][k]
            }
            return w.EndObject();
        }
    }
    return false;
}

static int api_json(lua_State *L)
{
    // TODO: might want to use an adapter for luaL_Buffer instead of this if this ever becomes a perf problem
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> wr(sb);
    bool strict = getBool(L, 2);
    if(emitJson(wr, L, 1, strict))
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
        if(!frames.empty())
        {
            Frame& f = frames.back();
            if(f.isobj)
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

    LuaJsonLoader(lua_State *L) : L(L) {}

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

static int api_loadjson(lua_State *L)
{
    LuaJsonLoader ld(L);
    const char *s = str(L);
    rapidjson::StringStream ss(s);
    if(!ld.parse(ss))
    {
        lua_pushnil(L);
        lua_pushstring(L, "parse error");
        return 2;
    }
    return 1;
}

static int api_opensocket(lua_State *L)
{
    const char *host = str(L, 1);
    const int port = (int)lua_tointeger(L, 2);
    bool ssl = getBool(L, 3);

    char errbuf[256];
    mg_connection *conn = mg_connect_client(host, port, ssl, errbuf, sizeof(errbuf));
    if(!conn)
    {
        lua_pushnil(L);
        lua_pushstring(L, errbuf);
        return 2;
    }

    // make pointer-sized heavy userdata and piggyback conn
    void *ud = lua_newuserdatauv(L, sizeof(mg_connection*), 0);
    *(mg_connection**)ud = conn;

    // assign metatable to make sure __gc is called if necessary
    int mt = luaL_getmetatable(L, "mg_connection");
    assert(mt == LUA_TTABLE);
    lua_setmetatable(L, -2);
    return 1;
}

static mg_connection *getConn(lua_State *L, int idx = 1)
{
    void *ud = luaL_checkudata(L, 1, "mg_connection");
    mg_connection *conn = *(mg_connection**)ud;
    return conn;
}

static int api_mg_connection__gc(lua_State *L)
{
    mg_connection *conn = getConn(L);
    mg_close_connection(conn);
    return 0;
}

static int api_mg_connection_write(lua_State* L)
{
    mg_connection* conn = getConn(L);
    size_t n;
    const char *s = luaL_checklstring(L, 2, &n);
    int sent = mg_write(conn, s, n);
    lua_pushinteger(L, sent);
    return 1;
}

static int api_mg_connection_readResponse(lua_State *L)
{
    mg_connection* conn = getConn(L);
    lua_Integer timeout = luaL_optinteger(L, 2, -1);
    // TODO: this should be in another thread so we don't block?
    char err[256];
    int resp = mg_get_response(conn, err, sizeof(err), (int)timeout);
    if(resp < 0)
    {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    const mg_response_info *ri = mg_get_response_info(conn);
    lua_pushinteger(L, ri->status_code);
    lua_pushstring(L, ri->status_text);
    lua_pushinteger(L, ri->content_length);
    lua_createtable(L, 0, ri->num_headers);
    for(int i = 0; i < ri->num_headers; ++i)
    {
        lua_pushstring(L, ri->http_headers[i].value);
        lua_setfield(L, -2, ri->http_headers[i].name);
    }
    return 4; // status, text, contentLen, {headers}
}

static int api_mg_connection_read(lua_State *L)
{
    mg_connection* conn = getConn(L);
    int maxrd = (int)luaL_optinteger(L, 2, -1);
    char buf[1024];
    int bufrd = sizeof(buf) < maxrd ? sizeof(buf) : maxrd;
    int rd = mg_read(conn, buf, bufrd);
    if(rd <= 0)
    {
gtfo:
        lua_pushnil(L);
        lua_pushstring(L, rd == 0 ? "closed" : "error");
        return 2;
    }
    if(rd < sizeof(buf)) // small read, just return what we have
    {
        lua_pushlstring(L, buf, rd);
        return 1;
    }
    // large read, concat pieces to large buffer
    maxrd -= rd;
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    luaL_addlstring(&b, buf, rd);

    while(maxrd)
    {
        int tord = std::min(bufrd, maxrd);
        rd = mg_read(conn, buf, tord);
        if (rd <= 0)
            goto gtfo;
        luaL_addlstring(&b, buf, rd);
        if(tord != rd) // drained recv socket; don't try again as that would make us wait
            break;
    }
    luaL_pushresult(&b);
    return 1;
}

static int api_base64enc(lua_State* L)
{
    size_t len;
    const char *s = luaL_checklstring(L, 1, &len);
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
    if(len && !base64dec(dec.data(), &declen, (unsigned char*)s, len))
        luaL_error(L, "base64dec() failed");
    lua_pushlstring(L, dec.data(), declen);
    return 1;
}

static const luaL_Reg reg[] =
{
    { "expect",    api_expect },
    { "skip",      api_skip },
    { "skipall",   api_skipall },
    { "need",      api_need },
    { "send",      api_send },
    { "readline",  api_readline },
    { "readn",     api_readn },
    { "peek",      api_peek },
    { "timeout",   api_timeout },
    { "json",      api_json },
    { "loadjson",  api_loadjson },
    { "opensocket",api_opensocket },
    { "base64enc", api_base64enc },
    { "base64dec", api_base64dec },
    { NULL,        NULL }
};

static const luaL_Reg mg_conn_reg[] =
{
    { "__gc",         api_mg_connection__gc },
    { "readResponse", api_mg_connection_readResponse },
    { "read",         api_mg_connection_read },
    { "write",        api_mg_connection_write },
    { NULL,           NULL }
};

void sisluafunc_register(lua_State *L, SISClient& cl)
{
    lua_pushglobaltable(L);
    luaL_setfuncs(L, reg, 0);
    lua_pop(L, 1);
    setclient(L, &cl);

    luaL_newmetatable(L, "mg_connection");
    luaL_setfuncs(L, mg_conn_reg, 0);
    lua_setfield(L, -1, "__index"); // set and pop self
}


static void luaImportVarArray(lua_State* L, VarCRef ref)
{
    const Var *a = ref.v->array();
    const size_t n = ref.size();
    lua_createtable(L, (int)n, 0);
    const int top = lua_gettop(L);
    for(size_t i = 0; i < n; ++i)
    {
        luaImportVar(L, VarCRef(ref.mem, &a[i]));
        lua_rawseti(L, -2, i+1);
    }
    assert(lua_gettop(L) == top);
}

static void luaImportVarMap(lua_State *L, VarCRef ref)
{
    const Var::Map *m = ref.v->map();
    lua_createtable(L, 0, (int)m->size());
    const int top = lua_gettop(L);
    for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
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
