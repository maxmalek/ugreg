#pragma once

#include <string>
#include <mutex>
#include "types.h"
#include "sissocket.h" 

struct SISClientConfig
{
    std::string name;
    std::string type;
    std::string host;
    unsigned port;
    // TODO: device
};

class SISClient
{
public:
    SISClient(const SISClientConfig& cfg);
    SISSocket connect(); // return new socket id
    SISSocket disconnect(); // close and return old socket id
    SISSocket invalidate(); // return old socket id
    void updateTimer(u64 dt);
    void updateIncoming();
    bool isConnected() const;

    std::mutex lock;

private:
    void heartbeat();

    SISSocket socket;
    u64 heartbeatTime;
    SISClientConfig cfg;
};
