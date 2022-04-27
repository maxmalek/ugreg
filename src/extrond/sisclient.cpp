#include "sisclient.h"
#include <assert.h>
#include <mutex>
#include <utility>
#include "minicoro.h"
#include "util.h"
#include "luafuncs.h"

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
    : socket(sissocket_invalid()), timeInState(0), state(DISCONNECTED), nextState(UNDEF)
    , inbufOffs(0), L(NULL), LA(NULL)
{
    cfg.name = name;
}

SISClient::~SISClient()
{
    if(L)
    {
        _abortScheduled();
        lua_close(L);
        luaalloc_delete(LA);
    }
}

bool SISClient::configure(VarCRef mycfg, VarCRef devcfg)
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

    if(!cfg.device.init(devcfg))
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

        luaImportVar(L, mycfg);
        // [t]
        lua_setglobal(L, "CONFIG");
        // []
        if (luaL_dofile(L, script) != LUA_OK)
        {
            const char *errmsg = lua_tostring(L, 1);
            printf("Failed to load script '%s', error:\n%s\n", script, errmsg);
            return false;
        }
        if(int top = lua_gettop(L)) // don't leave crap on the stack in case the body returns anything
            lua_pop(L, top);
        // []

        // enumerate global funcs
        lua_pushglobaltable(L);
        // [_G]
        const int t = lua_gettop(L);
        lua_pushnil(L);  /* first key */
        // [_G][nil]
        while (lua_next(L, t) != 0)
        {
            // [_G][k][v]
            if (lua_type(L, -2) == LUA_TSTRING && lua_isfunction(L, -1) && !lua_iscfunction(L, -1))
            {
                const char* name = lua_tostring(L, -2);
                if (name && *name && *name != '_')
                    luafuncs.insert(name);
            }
            lua_pop(L, 1);
            // [_G][k]
        }
        // [_G]
        lua_pop(L, 1);
        // []
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

void SISClient::_abortScheduled()
{
    assert(L);
    ActionResult fail;
    fail.error = true;
    fail.text = "Scheduled action cancelled because client entered error state";
    fail.status = 500;

    for(Job& j : jobs)
    {
        j.result.set_value(fail);
        j.unref(L);
    }
    jobs.clear();
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
    // Lock it, but don't bother if someone else is working on this client right now
    std::unique_lock<decltype(mtx)> g(mtx, std::try_to_lock);
    if(!g.owns_lock())
        return cfg.device.getIOYieldTime();

    timeInState += dt;

    if(jobs.size() > 1)
    {
        std::list<Job>::iterator it = jobs.begin();
        const Job& first = *it;
        ++it; // skip currently active job
        for ( ; it != jobs.end(); )
        {
            Job& j = *it;
            if(j.expiryTime && now > j.expiryTime)
            {
                printf("SISClient[%s]: ERROR: Action '%s' expired without ever starting; blocked by '%s'\n",
                    cfg.name.c_str(), j.actionName.c_str(), first.actionName.c_str());
                j.unref(L);
                ActionResult result;
                result.error = true;
                result.status = 408; // Request Timeout
                result.text = "ERROR: Expired while waiting for '" + first.actionName + "'";
                j.result.set_value(std::move(result));
                it = jobs.erase(it);
            }
            else
                ++it;
        }
    }

    u64 next = updateCoro();

    if(state == IDLE && timeInState > cfg.device.getHeartbeatTime())
        heartbeat();

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
    stateMaxTime = 0;

    switch(st)
    {
        case ERROR:
            _abortScheduled();
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
            break;

        case AUTHED:
            setState(IDLE);
            break;

        case IDLE:
            break;

        case INPROCESS:
            break;
    }
    return prev;
}



void SISClient::heartbeat()
{
    if(state == IDLE)
        scheduleAction("heartbeat", VarCRef(), INPROCESS, IDLE, IDLE, 0);
}

void SISClient::authenticate()
{
    assert(state == CONNECTED);
    // prefixed with "_" so that it's not callable via queryAsync() -- that would break things
    scheduleAction("_login", VarCRef(), AUTHING, AUTHED, ERROR, 0);
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

static SISClient::ActionResult funcNotExist(const std::string name)
{
    SISClient::ActionResult res;
    res.error = true;
    res.status = 500;
    res.text = "Action '" + name + "' failed. Unknown function?";
    return res;
}

std::future<SISClient::ActionResult> SISClient::scheduleAction(const char* name, VarCRef vars, State activestate, State donestate, State failstate, u64 expireIn)
{
    std::lock_guard<decltype(mtx)> g(mtx);

    // []
    int f = lua_getglobal(L, name);
    if(f != LUA_TFUNCTION)
    {
        printf("SISClient[%s]: ERROR: runAction [%s] is not a function\n", cfg.name.c_str(), name);
        lua_pop(L, 1);
        return std::async(funcNotExist, name);
    }
    // [f]
    lua_State * const Lco = lua_newthread(L);
    // [f][Lco]
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
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

    Job& j = jobs.emplace_back();
    j.Lparams = params;
    j.beginState = activestate;
    j.endState = donestate;
    j.failState = failstate;
    j.Lco = Lco;
    j.Lcoref = ref;
    j.actionName = name;
    j.expiryTime = expireIn ? timeNowMS() + expireIn : 0;

    printf("SISClient[%s]: Action '%s' coro is ready\n", cfg.name.c_str(), name);
    return j.result.get_future();
}

// Lua is supposed to return (text, status, contentType) with the latter two optional
// but for ease of use we'll accept the latter two in any order, whether present or not
// and also the first can be just a number in case of error
// so the following are all ok:
// return 404
// return text, 200
// return text, 200, "application/json"
// return text, "text/plain", 400
static void importLuaResult(SISClient::ActionResult& res, lua_State *L, int nres)
{
    if (nres)
    {
        {
            int t = lua_type(L, 1);
            if (t == LUA_TSTRING)
            {
                size_t len;
                const char *s = lua_tolstring(L, 1, &len);
                res.text.assign(s, len);
            }
            else if(t == LUA_TNUMBER)
                res.status = (unsigned)lua_tointeger(L, 1);
        }

        for(int i = 2; i < 4; ++i)
        {
            int t = lua_type(L, i);
            if(!res.status && t == LUA_TNUMBER)
            {
                res.status = (unsigned)lua_tointeger(L, i);
                ++i;
            }
            if(res.contentType.empty() && t == LUA_TSTRING)
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

u64 SISClient::updateCoro()
{
    u64 t = 0;
    bool gtfo = false;
    while(!jobs.empty() && !gtfo)
    {
        bool error = false;
        bool done = false;
        Job& j = jobs.front();
        if(!j.started)
        {
            j.started = true;
            setState(j.beginState);
        }

        int nres = 0;
        const int e = lua_resume(j.Lco, NULL, j.Lparams, &nres);
        j.Lparams = 0;
        switch (e)
        {
            case LUA_YIELD:
                if (nres)
                    lua_pop(j.Lco, nres);
                t = cfg.device.getIOYieldTime();
                gtfo = true;
            break;

            default:
                error = true;
            [[fallthrough]];
            case LUA_OK:
                done = true;
        }

        if(done)
        {
            ActionResult result;
            result.error = error;
            if(error)
            {
                result.status = 500;
                const char* errstr = lua_tostring(j.Lco, -1);
                printf("SISClient[%s]: ERROR: Lua coroutine failed (err = %d): %s\n", cfg.name.c_str(), e, errstr);
                if(errstr)
                    result.text = errstr;
            }
            else
            {
                importLuaResult(result, j.Lco, nres);
                if(!result.status)
                    result.status = 200;
            }

            j.unref(L); // now Lua will GC the coro eventually
            j.result.set_value(std::move(result));
            const State next = error ? j.failState : j.endState;
            jobs.pop_front();
            setState(next);
        }
        else
        {
            if (stateMaxTime && timeInState > stateMaxTime)
            {
                printf("SISClient[%s]: ERROR: Action '%s' timeout after %zu ms in same state\n", cfg.name.c_str(), j.actionName.c_str(), stateMaxTime);
                j.unref(L);
                ActionResult result;
                result.error = true;
                result.status = 408; // Request Timeout
                result.text = "ERROR: Timeout after " + std::to_string(stateMaxTime) + " ms in the same state";
                j.result.set_value(std::move(result));
                const State next = j.failState;
                jobs.pop_front();
                setState(next);
            }
        }
    }
    return t;
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

std::future<SISClient::ActionResult> SISClient::queryAsync(const char* action, VarCRef vars, u64 expireIn)
{
    // make sure the action to call is actually an exported user function, and not just a default lua global
    if(luafuncs.find(action) == luafuncs.end())
        return std::async(funcNotExist, action);

    return this->scheduleAction(action, vars, INPROCESS, IDLE, IDLE, expireIn);
}

SISClient::Job::Job()
    : Lco(NULL), Lcoref(LUA_NOREF), Lparams(0), beginState(UNDEF), endState(UNDEF), failState(UNDEF), started(false), expiryTime(0)
{
}

SISClient::Job::~Job()
{
    assert(!Lco);
}

void SISClient::Job::unref(lua_State* L)
{
    if(Lco)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, Lcoref);
        Lco = NULL;
        Lcoref = LUA_NOREF;
    }
}
