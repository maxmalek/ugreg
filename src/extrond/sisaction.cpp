#include <regex>
#include "sisaction.h"
#include "util.h"
#include "co.h"
#include "sisclient.h"

// Beware: These functions run in a coroutine that may or may not be killed.
// Means: Destructors may not be called. Don't allocate stuff on the heap in any of these functions!
typedef int (*CommFunc)(const SISComm& comm, SISClient& client);

enum
{
    BufferSize = 256,  // bytes
    IOWaitDelay = 10   // ms // TODO: make delay configurable?
};

enum ParamType
{
    AS_IS,
    AS_DURATION,
    AS_REGEX
};

struct SISCommDef
{
    const char *name;
    CommFunc f;
    Var::Type type;
    ParamType what;
    int params; // < 0: any
};

static int comm_expect(const SISComm& comm, SISClient& client)
{
    char buf[BufferSize];
    const char *beg = comm.paramStr.c_str();
    size_t remain = comm.paramStr.length();
    for(;;)
    {
        int rd = client.readInput(buf, std::min(sizeof(buf), remain));
        if(rd < 0)
            return rd;
        else if(!rd)
            co_yield1(IOWaitDelay);

        if(strncmp(buf, beg, remain))
            return -1; // mismatch

        beg += rd;
        remain -= rd;
    }
}

static int comm_match(const SISComm& comm, SISClient& client)
{
    // can't run regex on stream piecemeal, so just take the entire input buffer and
    // match as far as possible
    const char * const buf = client.getInputPtr();

    std::cmatch cm;
    // buf isn't a 0-terminated string -> need to use explicit slice
    if(!std::regex_search(buf, buf + client.availInput(), cm, comm.re, std::regex_constants::match_continuous))
        return -1;

    size_t matched = cm[0].length();
    client.advanceInput(matched);
    return (int)matched;
}

static int comm_skip(const SISComm& comm, SISClient& client)
{
    size_t remain = comm.paramNum;
    size_t done = 0;
    while(remain)
    {
        const size_t avail = client.availInput();
        if(avail)
        {
            const size_t skip = std::min(avail, remain);
            remain -= skip;
            done += skip;
            client.advanceInput(skip);
        }
        else
            co_yield1(IOWaitDelay);
    }
    return (int)done;
}

static int comm_skipall(const SISComm& comm, SISClient& client)
{
    size_t n = client.availInput();
    client.advanceInput(n);;
    return (int)n;
}

static int comm_wait(const SISComm& comm, SISClient& client)
{
    co_yield1(comm.paramNum);
    return 0;
}

static int comm_need(const SISComm& comm, SISClient& client)
{
    while(client.availInput() < comm.paramNum)
        co_yield1(IOWaitDelay);
    return 0;
}

static int comm_send(const SISComm& comm, SISClient& client)
{
    const char *s = comm.paramStr.c_str();
    size_t n = comm.paramStr.length();
    for(;;)
    {
        int sent = client.sendsome(s, n);
        if(sent < 0)
            return sent;

        if((size_t)sent == n)
            return (int)n;

        if ((size_t)sent < n)
        {
            s += sent;
            n -= sent;
            co_yield1(10);
        }
    }
}

static const SISCommDef actiondefs[] =
{
    { "expect",  comm_expect,  Var::TYPE_STRING, AS_IS,        1  }, // await string, success on exact match. wait if needed.
    { "match",   comm_match,   Var::TYPE_STRING, AS_REGEX,     1  }, // await string, success if it passes a regex. matches the current input buffer, never waits.
    { "skip",    comm_skip,    Var::TYPE_UINT,   AS_IS,        1  }, // skip exactly N bytes of input, wait if needed
    { "skipall", comm_skipall, Var::TYPE_NULL,   AS_IS,        0  }, // skip all bytes of input, never wait
    { "wait",    comm_wait,    Var::TYPE_STRING, AS_DURATION,  1  }, // wait for some time
    { "need",    comm_need,    Var::TYPE_UINT,   AS_IS,        1  }, // wait until N bytes can be read
    { "send",    comm_send,    Var::TYPE_STRING, AS_IS,        1  }, // send a string
};

SISComm::SISComm()
    : tableIndex(0), paramNum(0), re(NULL)
{
}

SISComm::~SISComm()
{
}

bool SISComm::parse(VarCRef a)
{
    assert(a.type() == Var::TYPE_ARRAY);
    const VarCRef vcmd = a.at(0);
    const SISCommDef *def = NULL;

    if(vcmd.type() != Var::TYPE_STRING)
        return false;

    const char *param = vcmd.asCString();

    for(size_t i = 0; i < Countof(actiondefs); ++i)
    {
        if(!strcmp(param, actiondefs[i].name))
        {
            def = &actiondefs[i];
            tableIndex = i;
            break;
        }
    }

    if(!def)
        return false;

    // TODO: fix this up once >2 params becomes a thing
    if(def->params)
    {
        const VarCRef vparam = a.at(1);

        if(vparam.type() != def->type)
            return false;

        if(vparam.type() == Var::TYPE_UINT)
            paramNum = *vparam.asUint();
        else if(vparam.type() == Var::TYPE_STRING)
        {
            if (def->what == AS_DURATION)
            {
                if (!strToDurationMS_Safe(&paramNum, vparam.asCString()))
                {
                    printf("Failed to parse as duration: %s\n", vparam.asCString());
                    return false;
                }
            }
            else if(def->what == AS_REGEX)
            {
                try
                {
                    std::regex r(vparam.asCString(), std::regex_constants::nosubs | std::regex_constants::optimize | std::regex_constants::ECMAScript);
                    re = std::move(r);
                }
                catch(std::regex_error e)
                {
                    printf("Regex compile error: %s\n", e.what());
                    return false;
                }
            }
            else
                paramStr = vparam.asCString();
        }
        else
            return false;
    }

    return true;
}

int SISComm::exec(SISClient& client) const
{
    return actiondefs[tableIndex].f(*this, client);
}

SISAction::SISAction()
{
}

bool SISAction::parse(VarCRef a)
{
    comms.clear();

    if(a.type() != Var::TYPE_ARRAY)
        return false;

    const size_t n = a.size();
    size_t offset = 0;
    for(size_t i = 0; i < n; ++i)
    {
        SISComm c;
        if(!c.parse(a.at(i)))
            return false;

        comms.push_back(std::move(c));
    }
    return true;
}

int SISAction::exec(SISClient& client) const
{
    int consumed = 0;
    for(size_t i = 0; i < comms.size(); ++i)
    {
        int res = comms[i].exec(client);
        if(res < 0)
            return res;
        consumed += res;
    }
    return consumed;
}
