#pragma once

#include <vector>

typedef uintptr_t SISSocket;

SISSocket sissocket_invalid();
bool sissocket_open(SISSocket* pHandle, const char* host, unsigned port);
void sissocket_close(SISSocket s);
size_t sissocket_read(SISSocket s, void* buf, size_t bufsize);
bool sissocket_write(SISSocket s, const void* buf, size_t bytes);


class SISSocketSet
{
public:
    SISSocketSet();
    ~SISSocketSet();
    void add(SISSocket s);

    enum EventFlags
    {
        CANREAD = 1,
        CANWRITE = 2,
        CANDISCARD = 4,
    };

    struct SocketAndStatus
    {
        SISSocket socket;
        unsigned flags; // EventFlags
    };

    SocketAndStatus* update(size_t* n, int timeoutMS);

private:
    void* _pfds; // opaque thingy so we don't have to leak OS socket defs to the outside
    std::vector<SocketAndStatus> _ready;
};
