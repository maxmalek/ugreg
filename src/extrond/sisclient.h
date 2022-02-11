#pragma once

#include <string>
#include <mutex>
#include "types.h"
#include "sissocket.h" 
#include "co.h"
#include "variant.h"
#include "sisdevice.h"

struct SISClientConfig
{
    SISClientConfig();
    std::string name;
    std::string host;
    unsigned port;
    SISDevice device;
};

class SISClient
{
public:
    SISClient(const char *name);
    bool configure(VarCRef mycfg, const SISDeviceTemplate& dev);
    SISSocket connect(); // return new socket id
    void disconnect();
    u64 updateTimer(u64 now, u64 dt);
    void updateIncoming(); // copy incoming data into inbuf
    void delayedConnected();
    bool isConnected() const;
    void wasDisconnected();

    std::mutex lock;

    enum State
    {
        ERROR,
        DISCONNECTED,
        CONNECTING,
        CONNECTED, // but not authed
        AUTHING, // authing right now
        AUTHED,
        IDLE, // authed not doing anything
        INPROCESS, // sending/receiving something right now

        _STATE_MAX
    };

    State setState(State st); /// returns prev state
    State getState() const { return state; }

    bool sendall(const char *buf, size_t size);
    int sendsome(const char *buf, size_t size); // <0: error, >0: this many bytes sent, ==0: can't send right now
    int readInput(char *dst, size_t bytes);
    size_t availInput() const { return inbuf.size(); }

    char *getInputPtr() { return inbuf.data(); }
    void advanceInput(size_t n);

    // client status
    const char *getStateStr() const;
    u64 getTimeInState() const { return timeInState; }
    const SISClientConfig& getConfig() const { return cfg; }
    std::string askStatus() const; // recording, paused, etc


private:
    void _clearBuffer();
    void _disconnect();
    void heartbeat();
    void authenticate();
    bool co_runAction(const char *name, State nextstate);

    static void co_task_auth(void *me, size_t delay);
    static void co_task_heartbeat(void *me, size_t delay);

    SISSocket socket;
    u64 heartbeatTime;
    u64 timeInState;
    SISClientConfig cfg;
    State state;
    CoroRunner tasks;
    std::vector<char> inbuf;
    size_t inbufOffs;
};
