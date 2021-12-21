#include "sisclient.h"
#include <assert.h>


SISClient::SISClient(const SISClientConfig& cfg)
    : cfg(cfg), socket(sissocket_invalid()), heartbeatTime(0)
{
}

SISSocket SISClient::connect()
{
    printf("Connecting to %s:%u ...\n", cfg.host.c_str(), cfg.port);
    SISSocket s = sissocket_invalid();
    assert(socket == s);
    if(sissocket_open(&socket, cfg.host.c_str(), cfg.port))
        socket = s;
    return s;
}

SISSocket SISClient::disconnect()
{
    assert(socket != sissocket_invalid());
    sissocket_close(socket);
    return invalidate();
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
        if(heartbeatTime < dt)
            heartbeat();
    }
}

bool SISClient::isConnected() const
{
    return socket != sissocket_invalid();
}

void SISClient::updateIncoming()
{
    char buf[1024];
    size_t rd;
    do
    {
        rd = sissocket_read(socket, buf, sizeof(buf));
        printf("Socket[%p] read %u bytes\n", (void*)socket, (unsigned)rd);
    }
    while(rd);
}

void SISClient::heartbeat()
{
    // TODO: invoke VM
    // TODO: reset timer
}
