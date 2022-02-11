#include "sisclient.h"
#include <assert.h>
#include "minicoro.h"
#include "util.h"

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
    : socket(sissocket_invalid()), heartbeatTime(0), timeInState(0), state(DISCONNECTED)
    , inbufOffs(0)
{
    cfg.name = name;
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

    return cfg.device.init(dev, mycfg);
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
    u64 next = tasks.update(now);
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

        case AUTHED:
            setState(IDLE);
            break;

        case IDLE:
            heartbeatTime = cfg.device.getHeartbeatTime();
            break;
    }
    return prev;
}

void SISClient::heartbeat()
{
    if(state == IDLE)
    {
        tasks.scheduleIn(co_task_heartbeat, this, 0);
    }
}

void SISClient::authenticate()
{
    assert(state == CONNECTED);
    tasks.scheduleIn(co_task_auth, this, 0);
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

bool SISClient::co_runAction(const char* name, State nextstate)
{
    const SISAction* act = cfg.device.getAction(name);
    if (!act)
    {
        printf("SISClient[%s]: Action '%s' does not exist\n", cfg.name.c_str(), name);
        return false;
    }
    printf("SISClient[%s]: Action '%s' starting...\n", cfg.name.c_str(), name);
    setState(nextstate);
    int result = act->exec(*this);
    printf("SISClient[%s]: Action '%s' %s with return %d\n", cfg.name.c_str(), name, result < 0 ? "failed" : "completed", result);
    return result  >= 0;
}

void SISClient::co_task_auth(void* me, size_t delay)
{
    SISClient* self = (SISClient*)me;
    if(self->co_runAction("login", AUTHING))
        self->setState(AUTHED);
    else
        self->setState(ERROR);
}

void SISClient::co_task_heartbeat(void* me, size_t delay)
{
    SISClient *self = (SISClient*)me;
    self->co_runAction("heartbeat", INPROCESS);
    if(self->state == INPROCESS)
        self->setState(IDLE);
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

    /*if (!isConnected())
        return -999;

    size_t rd = 0;
    SocketIOResult res = sissocket_read(socket, dst, &rd, bytes);
    if(res == SOCKIO_OK || res == SOCKIO_TRYLATER)
        return (int)rd;

    assert(res > 0); // make sure the resulting error code is negative
    return -(int)res;*/
}


const char* SISClient::getStateStr() const
{
    return s_StateNames[getState()];
}

std::string SISClient::askStatus() const
{
    return "NYI";
}

