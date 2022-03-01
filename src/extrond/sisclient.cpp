#include "sisclient.h"
#include <assert.h>
#include <mutex>
#include <utility>
#include "minicoro.h"
#include "util.h"
#include <lua/lua.hpp>

static const char* const s_StateNames[] =
{
    "ERROR",
    "DISCONNECTED",
    "CONNECTING",
    "CONNECTED",
    "AUTHING",
    "AUTHED",
    "IDLE",
    "INPROCESS"
};
static_assert(Countof(s_StateNames) == SISClient::_STATE_MAX, "size mismatch");


SISClientConfig::SISClientConfig()
    : port(23)
{
}

SISClient::SISClient(const char *name)
    : socket(sissocket_invalid()), heartbeatTime(0), timeInState(0), state(DISCONNECTED), nextState(UNDEF)
    , inbufOffs(0), lock(mtx, std::defer_lock), L(NULL), LA(NULL), activeL(NULL), activeLRef(LUA_NOREF)
{
    cfg.name = name;
}

SISClient::~SISClient()
{
    if(L)
    {
        lua_close(L);
        luaalloc_delete(LA);
    }
}

bool SISClient::configure(VarCRef mycfg, const SISDeviceTemplate& dev)
{
    const VarCRef xhost = mycfg.lookup("host");
    const VarCRef xport = mycfg.lookup("port");
    const char* host = xhost ? xhost.asCString() : NULL;
    if(unsigned port = unsigned(xport && xport.asUint() ? *xport.asUint() : 0))
        cfg.port = port;

    printf("New device: '%s' = %s:%u\n", cfg.name.c_str(), host, cfg.port);
    if (!(host && *host))
        return false;

    cfg.host = host;

    if(!cfg.device.init(dev, mycfg))
        return false;

    luafuncs.clear();
    if(const char *script = cfg.device.getScript())
    {
        LuaAlloc *LA = luaalloc_create(NULL, NULL);
        lua_State *L = lua_newstate(luaalloc, LA);
        this->LA = LA;
        this->L = L;
        luaL_openlibs(L);
        sisluafunc_register(L, *this);

        if(luaL_dofile(L, script) != LUA_OK)
        {
            printf("Failed to load script '%s'\n", script);
            return false;
        }

        // enumerate global funcs
        lua_pushglobaltable(L);
        // [_G]
        const int t = lua_gettop(L);
        lua_pushnil(L);  /* first key */
        // [_G][nil]
        while (lua_next(L, t) != 0)
        {
            // [_G][k][v]
            if(lua_type(L, -2) == LUA_TSTRING && lua_isfunction(L, -1) && !lua_iscfunction(L, -1))
            {
                const char *name = lua_tostring(L, -2);
                if(name && *name && *name != '_')
                {
                    printf("Lua func: %s\n", name);
                    luafuncs.insert(name);
                }
            }
            lua_pop(L, 1);
            // [_G][k]
        }
        // [_G]
        lua_pop(L, 1);
        // []

        luaImportVar(L, mycfg);
        // [t]
        lua_setglobal(L, "CONFIG");
    }

    return true;
}

SISSocket SISClient::connect()
{
    disconnect();
    printf("Connecting to %s (%s:%u) ...\n", cfg.name.c_str(), cfg.host.c_str(), cfg.port);
    _clearBuffer();
    SocketIOResult res = sissocket_open(&socket, cfg.host.c_str(), cfg.port);
    if(res == SOCKIO_OK)
    {
        printf("Connected to %s (%s:%u), socket = %p\n",
            cfg.name.c_str(), cfg.host.c_str(), cfg.port, (void*)socket);
        setState(CONNECTED);
    }
    else if(res == SOCKIO_TRYLATER)
        setState(CONNECTING);
    else
        setState(ERROR);
    return socket;
}

void SISClient::disconnect()
{
    setState(DISCONNECTED);
}

void SISClient::_disconnect()
{
    printf("Disconnect %s (%s:%u), socket = %p\n",
        cfg.name.c_str(), cfg.host.c_str(), cfg.port, (void*)socket);
    SISSocket inv = sissocket_invalid();
    if(socket != inv)
    {
        sissocket_close(socket);
        socket = inv;
    }
}

void SISClient::_clearBuffer()
{
    inbuf.clear();
    inbufOffs = 0;
}

void SISClient::wasDisconnected()
{
    printf("Disconnected from %s (%s:%u) by remote end, socket was %p\n",
        cfg.name.c_str(), cfg.host.c_str(), cfg.port, (void*)socket);
    if(state > DISCONNECTED)
        setState(DISCONNECTED);
    else
        _disconnect();
}

u64 SISClient::updateTimer(u64 now, u64 dt)
{
    // Lock it, but don't bother if someone else if working on this client already
    std::unique_lock<decltype(mtx)> g(mtx, std::try_to_lock);
    if(!g.owns_lock())
        return cfg.device.getIOYieldTime();

    u64 next = 0; //tasks.update(now);

    ActionResult res;
    next = updateCoro(res, 0);
    if(res.nret)
        printf("Lua result ignored: [%u] %s\n", res.status, res.text.c_str());

    if(state == IDLE)
    {
        if(heartbeatTime > dt)
            heartbeatTime -= dt;
        else
            heartbeat();
    }
    timeInState += dt;

    if(state == ERROR && timeInState > 3000) // give it some time
        setState(DISCONNECTED);

    return next;
}

bool SISClient::isConnected() const
{
    return socket != sissocket_invalid();
}


void SISClient::updateIncoming()
{
    char buf[1024];
    size_t rd;
    for(;;)
    {
        SocketIOResult res = sissocket_read(socket, buf, &rd, sizeof(buf));
        if(res == SOCKIO_OK || res == SOCKIO_TRYLATER)
        {
            if(rd)
            {
                printf("[%s]: ", cfg.name.c_str());
                fwrite(buf, 1, rd, stdout);
                size_t oldsize = inbuf.size();
                inbuf.resize(oldsize + rd);
                memcpy(inbuf.data() + oldsize, buf, rd);
            }
            else
                break;
        }
        else
        {
            disconnect();
            break;
        }
    }
}

void SISClient::delayedConnected()
{
    if(state == CONNECTING)
        setState(CONNECTED);
    else
        setState(ERROR);
}


SISClient::State SISClient::setState(State st)
{
    const State prev = state;
    if(prev == st)
        return prev;
    printf("SISClient[%s]: State %s -> %s, timeInState = %u\n",
        cfg.name.c_str(), s_StateNames[state], s_StateNames[st], (unsigned)timeInState);
    state = st;
    timeInState = 0;

    switch(st)
    {
        case ERROR:
            if(lock)
                lock.unlock();
            [[fallthrough]];

        case DISCONNECTED:
            _clearBuffer();
            if(isConnected())
                _disconnect(); // don't change state, linger in error state for a bit
        break;

        case CONNECTED:
            _clearBuffer();
            if(isConnected())
                authenticate();
            else
                setState(ERROR);
        break;

        case AUTHING:
            lock.lock();
            break;

        case AUTHED:
            setState(IDLE);
            break;

        case IDLE:
            heartbeatTime = cfg.device.getHeartbeatTime();
            lock.unlock();
            break;

        case INPROCESS:
            lock.lock();
            break;
    }
    return prev;
}

void SISClient::heartbeat()
{
    if(state == IDLE)
        runAction("heartbeat", VarCRef(), INPROCESS, IDLE);
}

void SISClient::authenticate()
{
    assert(state == CONNECTED);
    if (!runAction("_login", VarCRef(), AUTHING, AUTHED))
        setState(ERROR);
}

bool SISClient::runAction(const char* name, VarCRef vars, State activestate, State afterwards)
{
    ActionResult res;
    const bool ret = runAction(res, name, vars, activestate, afterwards);
    if (res.nret)
        printf("Lua result ignored: [%u] %s\n", res.status, res.text.c_str());
    return ret;
}

bool SISClient::sendall(const char* buf, size_t size)
{
    if(!isConnected())
        return false;

    size_t wr;
    return sissocket_write(socket, buf, &wr, size) == SOCKIO_OK; // FIXME: handle errors
}

int SISClient::sendsome(const char* buf, size_t size)
{
    if (!isConnected())
        return -999;

    size_t wr = 0;
    SocketIOResult res = sissocket_write(socket, buf, &wr, size);
    if(res == SOCKIO_OK || res == SOCKIO_TRYLATER)
        return (int)wr;

    assert(res > 0); // make sure the resulting error code is negative
    return -(int)res;
}

bool SISClient::runAction(ActionResult& result, const char* name, VarCRef vars, State activestate, State afterwards)
{
    std::lock_guard<decltype(mtx)> g(mtx);

    assert(!activeL);

    // []
    int f = lua_getglobal(L, name);
    if(f != LUA_TFUNCTION)
    {
        printf("SISClient: runAction [%s] is not a function\n", name);
        lua_pop(L, 1);
        return false;
    }
    // [f]
    lua_State *Lco = lua_newthread(L);
    // [f][Lco]
    activeL = Lco;
    // [f][Lco]
    activeLRef = luaL_ref(L, LUA_REGISTRYINDEX);
    // [f]
    lua_xmove(L, Lco, 1);
    // L:   []
    // Lco: [f]
    int params = 0;
    if(vars)
    {
        luaImportVar(Lco, vars);
        ++params;
    }
    // Lco: [f][vars?]

    printf("SISClient[%s]: Action '%s' coro is ready\n", cfg.name.c_str(), name);
    setState(activestate);
    nextState = afterwards;
    updateCoro(result, params); // may or may not finish running the coroutine
    return true;
}

// Lua is supposed to return (text, status, contentType) with the latter two optional
// but for ease of use we'll accept the latter two in any order, whether present or not
// and also the first an be just a number in case of error
// so the following are all ok:
// return 404
// return text, 200
// return text, 200, "application/json"
// return text, "text/plain", 400
static void importLuaResult(SISClient::ActionResult& res, lua_State *L, int nres)
{
    if (nres)
    {
        if (lua_isstring(L, 1))
        {
            size_t len;
            const char *s = lua_tolstring(L, 1, &len);
            res.text.assign(s, len);
        }
        else if(lua_isnumber(L, 1))
            res.status = (unsigned)lua_tointeger(L, 1);

        for(int i = 2; i < 4; ++i)
        {
            if(!res.status && lua_isnumber(L, i))
            {
                res.status = (unsigned)lua_tointeger(L, i);
                ++i;
            }
            if(res.contentType.empty() && lua_isstring(L, i))
            {
                size_t len;
                const char* s = lua_tolstring(L, i, &len);
                res.contentType.assign(s, len);
            }
        }
        lua_pop(L, nres);
    }
    res.nret = nres;
}

u64 SISClient::updateCoro(ActionResult& result, int nargs)
{
    u64 next = cfg.device.getIOYieldTime(); // FIXME: use some other time?
    if (activeL)
    {
        int nres = 0;
        int e = lua_resume(activeL, NULL, nargs, &nres);
        switch (e)
        {
        case LUA_YIELD:
            next = cfg.device.getIOYieldTime();
            if (nres)
                lua_pop(activeL, nres);
            break;

        default:
        {
            result.error = true;
            result.status = 500;
            const char* errstr = lua_tostring(activeL, -1);
            printf("ERROR: Lua coroutine failed (err = %d): %s\n", e, errstr);
            if(errstr)
                result.text = errstr;
        }
        [[fallthrough]];
        case LUA_OK:
            result.done = true;
            importLuaResult(result, activeL, nres);
        }
    }

    if(result.done)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, activeLRef);
        activeLRef = LUA_NOREF;
        activeL = NULL;
        const State st = nextState;
        if (st != UNDEF)
        {
            nextState = UNDEF;
            setState(st);
        }
    }

    return next;
}

void SISClient::advanceInput(size_t n)
{
    size_t avail = inbuf.size() - inbufOffs;
    assert(n <= avail);
    inbufOffs += n;
    if (inbufOffs >= inbuf.size())
    {
        inbuf.clear();
        inbufOffs = 0;
    }
}

// read incoming data into an internal buffer so that accumulating a certain number of bytes somewhere inside a coroutine is easier
int SISClient::readInput(char* dst, size_t bytes)
{
    if(!isConnected())
        return -999;

    size_t rd = std::min(inbuf.size() - inbufOffs, bytes);
    if(rd)
    {
        memcpy(dst, inbuf.data() + inbufOffs, rd);
        inbufOffs += rd;
        if(inbufOffs >= inbuf.size())
        {
            inbuf.clear();
            inbufOffs = 0;
        }
    }
    return (int)rd;
}


const char* SISClient::getStateStr() const
{
    return s_StateNames[getState()];
}

std::string SISClient::askStatus()
{
    return state < IDLE ? "" : query("status", VarCRef()).text;
}

SISClient::ActionResult SISClient::query(const char* action, VarCRef vars)
{
    std::lock_guard<decltype(mtx)> g(mtx);

    ActionResult res;

    // make sure the action to call is actually an exported user function, and not just a default lua global
    if(luafuncs.find(action) != luafuncs.end()
        && this->runAction(res, action, vars, INPROCESS, IDLE))
    {
        while(!res.done)
        {
            u64 t = updateCoro(res, 0);
            sleepMS(std::max<u64>(t, 10));
        }
    }
    else
    {
        res.error = true;
        res.status = 404;
        res.text = "Unknown action: ";
        res.text += action;
    }
    return res;
}
