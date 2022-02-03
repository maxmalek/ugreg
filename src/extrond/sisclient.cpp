#include "sisclient.h"
#include <assert.h>
#include "minicoro.h"

SISClientConfig::SISClientConfig()
    : port(23), heartbeatInterval(30000)
{
}



SISClient::SISClient(const char *name)
    : socket(sissocket_invalid()), heartbeatTime(0), timeInState(0), state(DISCONNECTED)
    , inbufOffs(0)
{
    cfg.name = name;
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

    return true;
}

SISSocket SISClient::connect()
{
    printf("Connecting to %s (%s:%u) ...\n", cfg.name.c_str(), cfg.host.c_str(), cfg.port);
    SISSocket s = sissocket_invalid();
    assert(socket == s);
    if(sissocket_open(&s, cfg.host.c_str(), cfg.port))
    {
        printf("Connected to %s (%s:%u), using config '%s', socket = %p\n",
            cfg.name.c_str(), cfg.host.c_str(), cfg.port, cfg.type.c_str(), (void*)s);
        socket = s;
        setState(CONNECTED);
    }
    else
        setState(ERROR);
    return s;
}

SISSocket SISClient::disconnect()
{
    printf("Disconnect %s (%s:%u), socket = %p\n",
        cfg.name.c_str(), cfg.host.c_str(), cfg.port, (void*)socket);
    assert(socket != sissocket_invalid());
    sissocket_close(socket);
    setState(DISCONNECTED);
    return invalidate();
}

void SISClient::wasDisconnected()
{
    printf("Disconnected from %s (%s:%u) by remote end, socket was %p\n",
        cfg.name.c_str(), cfg.host.c_str(), cfg.port, (void*)socket);
    setState(DISCONNECTED);
    invalidate();
}

SISSocket SISClient::invalidate()
{
    SISSocket old = socket;
    socket = sissocket_invalid();
    return old;
}

void SISClient::updateTimer(u64 dt)
{
    if(socket != sissocket_invalid())
    {
        if(heartbeatTime > dt)
            heartbeatTime -= dt;
        else
            heartbeat();
    }
    timeInState += dt;

    if(state == ERROR && timeInState > 3000) // give it some time
        setState(DISCONNECTED);
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


void SISClient::setState(State st)
{
    const State prev = state;
    state = st;
    timeInState = 0;

    switch(st)
    {
        case ERROR:
            if(isConnected())
                disconnect();
            else
                invalidate();
        break;

        case CONNECTED:
            if(isConnected())
                authenticate();
            else
                setState(ERROR);
    }
}

void SISClient::heartbeat()
{
    if(state == IDLE)
    {
        // TODO: invoke VM
        // TODO: reset timer
    }
    heartbeatTime = cfg.heartbeatInterval;
}

void SISClient::authenticate()
{
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

void SISClient::co_task_auth(void* me, size_t delay)
{
    SISClient *self = (SISClient*)me;

    char buf[64];


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

