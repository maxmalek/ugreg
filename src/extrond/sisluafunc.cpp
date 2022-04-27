#include <string.h>
#include <sstream>
#include <lua/lua.hpp>
#include "sisclient.h"
#include "luafuncs.h"
#include "util.h"

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

static const luaL_Reg funcs[] =
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
    { NULL,        NULL }
};

static void makeMT(lua_State *L, const luaL_Reg *reg, const char *name)
{
    luaL_newmetatable(L, name);
    luaL_setfuncs(L, reg, 0);
    lua_setfield(L, -1, "__index"); // set and pop self
}

void sisluafunc_register(lua_State *L, SISClient& cl)
{
    lua_pushglobaltable(L);
    luaL_setfuncs(L, funcs, 0);
    lua_pop(L, 1);
    setclient(L, &cl);
}
