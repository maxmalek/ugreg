#pragma once

#include <string>
#include <mutex>
#include <set>
#include <future>
#include <list>
#include "types.h"
#include "sissocket.h" 
#include "co.h"
#include "variant.h"
#include "sisdevice.h"
#include "sisluafunc.h"
#include "luaalloc.h"

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
    ~SISClient();
    bool configure(VarCRef mycfg, VarCRef devcfg);
    SISSocket connect(); // return new socket id
    void disconnect();
    u64 updateTimer(u64 now, u64 dt);
    void updateIncoming(); // copy incoming data into inbuf
    void delayedConnected();
    bool isConnected() const;
    void wasDisconnected();

    enum State
    {
        UNDEF = -1,
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

    State setState(State st); // returns prev state
    State getState() const { return state; }

    bool sendall(const char *buf, size_t size);
    int sendsome(const char *buf, size_t size); // <0: error, >0: this many bytes sent, ==0: can't send right now
    int readInput(char *dst, size_t bytes);
    size_t availInput() const { return inbuf.size() - inbufOffs; }

    char *getInputPtr() { return inbuf.data() + inbufOffs; }
    void advanceInput(size_t n);

    // client status
    const char *getStateStr() const;
    u64 getTimeInState() const { return timeInState; }
    const SISClientConfig& getConfig() const { return cfg; }

    struct ActionResult
    {
        inline ActionResult() : status(0), nret(0), error(false) {}
        std::string text;
        std::string contentType;
        unsigned status;
        unsigned nret;
        bool error;
    };

    // scheduled or running job
    struct Job
    {
        Job();
        ~Job();
        void unref(lua_State *L); // call this before dtor
        lua_State *Lco;
        int coref;
        std::promise<ActionResult> result;
        State beginState;
        State endState;
        State failState;
        bool started;
    };

    typedef std::future<ActionResult> FutureResult;

    FutureResult queryAsync(const char *action, VarCRef vars);

private:
    void _abortScheduled();
    void _clearBuffer();
    void _disconnect();
    void heartbeat();
    void authenticate();
    FutureResult scheduleAction(const char *name, VarCRef vars, State activestate, State donestate, State failstate);
    u64 updateCoro();

    SISSocket socket;
    u64 timeInState;
    SISClientConfig cfg;
    State state, nextState;
    //CoroRunner tasks;
    std::vector<char> inbuf;
    size_t inbufOffs;
    std::recursive_mutex mtx;
    lua_State *L;
    LuaAlloc *LA;
    std::list<Job> jobs; // always work on the head

    std::set<std::string> luafuncs; // available Lua global funcs exported by the script
};
