#pragma once

#include <string>
#include <mutex>
#include "types.h"
#include "sissocket.h" 
#include "co.h"
#include "variant.h"

struct SISClientConfig
{
    SISClientConfig();
    std::string name;
    std::string type;
    std::string host;
    unsigned port;
    u64 heartbeatInterval;
};

class SISClient
{
public:
    SISClient(const char *name);
    bool configure(VarCRef mycfg, VarCRef devcfg);
    SISSocket connect(); // return new socket id
    SISSocket disconnect(); // close and return old socket id
    void updateTimer(u64 dt);
    void updateIncoming(); // copy incoming data into inbuf
    bool isConnected() const;
    void wasDisconnected();

    std::mutex lock;

    enum State
    {
        ERROR,
        DISCONNECTED,
        CONNECTED, // but not authed
        AUTHED,
        IDLE, // authed not doing anything
        INPROCESS, // sending/receiving something right now
    };

    void setState(State st);
    State getState() const { return state; }

    bool sendall(const char *buf, size_t size);
    int sendsome(const char *buf, size_t size); // <0: error, >0: this many bytes sent, ==0: can't send right now
    int readInput(char *dst, size_t bytes);
    size_t availInput() const { return inbuf.size(); }

    char *getInputPtr() { return inbuf.data(); }
    void advanceInput(size_t n);

private:
    SISSocket invalidate(); // return old socket id
    void heartbeat();
    void authenticate();

    static void co_task_auth(void *me, size_t delay);

    SISSocket socket;
    u64 heartbeatTime;
    u64 timeInState;
    SISClientConfig cfg;
    State state;
    CoroRunner tasks;
    std::vector<char> inbuf;
    size_t inbufOffs;
};
