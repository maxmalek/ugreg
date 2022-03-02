#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif


#include "sissocket.h"

#include <limits.h>
#include <assert.h>

#ifdef _WIN32
/*#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0501
#  endif*/
#  define FD_SETSIZE 1024
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  ifndef EWOULDBLOCK
#    define EWOULDBLOCK WSAEWOULDBLOCK
#  endif
#  ifndef ETIMEDOUT
#    define ETIMEDOUT WSAETIMEDOUT
#  endif
#  ifndef ECONNRESET
#    define ECONNRESET WSAECONNRESET
#  endif
#  ifndef ENOTCONN
#    define ENOTCONN WSAENOTCONN
#  endif
#  include <io.h>
#else
#  include <sys/types.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netdb.h>
#  include <sys/select.h>
#  define SOCKET_ERROR (-1)
#  define INVALID_SOCKET (SOCKET)(~0)
typedef intptr_t SOCKET;
#endif

#include <stdio.h>
#include <string>

#define SOCKETVALID(s) ((s) != INVALID_SOCKET)

#ifdef _DEBUG
#  define traceprint(...) {printf(__VA_ARGS__);}
#else
#  define traceprint(...) {}
#endif

inline int _GetError()
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

inline std::string _GetErrorStr(int e)
{
    std::string ret;
#ifdef _WIN32
    LPTSTR s;
    ::FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, e, 0, (LPTSTR)&s, 0, NULL);
    if (s)
        ret = s;
    ::LocalFree(s);
#else
    const char* s = strerror(e);
    if (s)
        ret = s;
#endif
    return ret;
}

static bool _Resolve(const char* host, unsigned port, struct sockaddr_in* addr)
{
    char port_str[16];
    sprintf(port_str, "%u", port);

    struct addrinfo hnt, * res = 0;
    memset(&hnt, 0, sizeof(hnt));
    hnt.ai_family = AF_INET;
    hnt.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_str, &hnt, &res))
    {
        traceprint("RESOLVE ERROR: %s", _GetErrorStr(_GetError()).c_str());
        return false;
    }
    if (res)
    {
        if (res->ai_family != AF_INET)
        {
            traceprint("RESOLVE WTF: %s", _GetErrorStr(_GetError()).c_str());
            freeaddrinfo(res);
            return false;
        }
        memcpy(addr, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);
        return true;
    }
    return false;
}

static bool _SetNonBlocking(SOCKET s, bool nonblock)
{
    if (!SOCKETVALID(s))
        return false;
#ifdef _WIN32
    ULONG tmp = !!nonblock;
    if (::ioctlsocket(s, FIONBIO, &tmp) == SOCKET_ERROR)
        return false;
#else
    int tmp = ::fcntl(s, F_GETFL);
    if (tmp < 0)
        return false;
    if (::fcntl(s, F_SETFL, nonblock ? (tmp | O_NONBLOCK) : (tmp |= ~O_NONBLOCK)) < 0)
        return false;
#endif
    return true;
}

SISSocket sissocket_invalid()
{
    return INVALID_SOCKET;
}

SocketIOResult sissocket_open(SISSocket *pHandle, const char* host, unsigned port)
{
    sockaddr_in addr;
    if (!_Resolve(host, port, &addr))
    {
        traceprint("RESOLV ERROR: %s\n", _GetErrorStr(_GetError()).c_str());
        return SOCKIO_FAILED;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (!SOCKETVALID(s))
    {
        traceprint("SOCKET ERROR: %s\n", _GetErrorStr(_GetError()).c_str());
        return SOCKIO_FAILED;
    }

    SocketIOResult ret = SOCKIO_FAILED;

    if (!_SetNonBlocking(s, true))
    {
        traceprint("_SetNonBlocking failed: %s\n", _GetErrorStr(_GetError()).c_str());
        ret = SOCKIO_FAILED;
    }
    else
    {
        if (!::connect(s, (sockaddr*)&addr, sizeof(sockaddr)))
            ret = SOCKIO_OK;
        else
        {
            int err = _GetError();
            switch(err)
            {
            case EINPROGRESS:
#ifdef WSAEINPROGRESS
            case WSAEINPROGRESS:
#endif
#ifdef WSAEWOULDBLOCK
            case WSAEWOULDBLOCK: // windows uses this for some reason
#endif
                traceprint("SOCKET[%p]: Connection to '%s:%u' in progress...\n", (void*)s, host, port);
                ret = SOCKIO_TRYLATER;
                break;
            default:
                traceprint("CONNECT ERROR %d: %s\n", err, _GetErrorStr(err).c_str());
                ret = SOCKIO_CLOSED;
            }
        }
    }
    
    if(!(ret == SOCKIO_OK || ret == SOCKIO_TRYLATER))
    {
        sissocket_close(s);
        s = INVALID_SOCKET;
    }

    *pHandle = s;
    return ret;
}

void sissocket_close(SISSocket s)
{
#ifdef _WIN32
    ::closesocket((SOCKET)s);
#else
    ::close(_s);
#endif
}

static SocketIOResult getIOError()
{
    int err = _GetError();
    switch(err)
    {
        case EAGAIN: 
        case EWOULDBLOCK:
#ifdef WSAEWOULDBLOCK
        case WSAEWOULDBLOCK:
#endif
            return SOCKIO_TRYLATER;
    }
    printf("Unhandled socket IO error %d\n", err);
    perror("");
    return SOCKIO_FAILED;
}

SocketIOResult sissocket_read(SISSocket s, void* buf, size_t *rdsize, size_t bufsize)
{
    assert(bufsize && bufsize <= INT_MAX);
    *rdsize = 0;
    if(bufsize)
    {
        int ret = ::recv((SOCKET)s, (char*)buf, (int)bufsize, 0);
        if(ret <= 0)
            return ret == 0 ? SOCKIO_CLOSED : getIOError();
        *rdsize = (unsigned)ret;
    }
    return SOCKIO_OK;
}

SocketIOResult sissocket_write(SISSocket s, const void* buf, size_t *wrsize, size_t bytes)
{
    assert(bytes <= INT_MAX);
    *wrsize = 0;
    if(bytes)
    {
        int flags = 0;
    #ifdef MSG_NOSIGNAL
        flags |= MSG_NOSIGNAL;
    #endif
        int ret = ::send((SOCKET)s, (const char*)buf, (int)bytes, flags);
        if(ret <= 0)
            return ret == 0 ? SOCKIO_CLOSED : getIOError();
        *wrsize = (unsigned)ret;
    }
    return SOCKIO_OK;
}

typedef std::vector<pollfd> PollVec;
#define PV (*((PollVec*)(this->_pfds)))

SISSocketSet::SISSocketSet()
    : _pfds(new PollVec)
{
}

SISSocketSet::~SISSocketSet()
{
    for (size_t i = 0; i < PV.size(); ++i)
        sissocket_close(PV[i].fd);
    delete& PV;
}

void SISSocketSet::add(SISSocket s)
{
    pollfd p;
    p.fd = s;
    p.events = POLLIN | POLLOUT; // TODO: set pollout flag only when we actually have data to send that were unable to be sent earlier
    p.revents = 0;
    PV.push_back(p);
}

SISSocketSet::SocketAndStatus* SISSocketSet::update(size_t* n, int timeoutMS)
{
    size_t N = PV.size();
    int ret;
#ifdef _WIN32
    if(!N) // windows doesn't like WSAPoll() on an empty set, so if there's nothing to wait on just sleep manually
    {
        if(timeoutMS)
            ::Sleep(timeoutMS);
        *n = 0;
        return NULL;
    }
    ::WSASetLastError(0);
    ret = ::WSAPoll(PV.data(), (ULONG)N, timeoutMS);
#else
    ret = ::poll(PV.data(), N, timeoutMS);
#endif
    _ready.clear();
    if (ret <= 0)
    {
#ifdef _WIN32
        if(ret < 0)
        {
            int err = ::WSAGetLastError();
            printf("WSAGetLastError() = %d, N = %u\n", err, unsigned(N));
        }
#endif
        *n = 0;
        return NULL;
    }

    for (size_t i = 0; i < PV.size(); )
    {
        pollfd& p = PV[i];
        if (!p.revents)
        {
            ++i;
            continue;
        }

        SocketAndStatus ss;
        ss.socket = p.fd;
        ss.flags = 0;

        // partially open socket where we're still waiting for writability
        if(p.events & POLLOUT)
        {
            p.events &= ~POLLOUT;
            int soerr;
            int soerrsize = sizeof(soerr);
            if(::getsockopt(p.fd, SOL_SOCKET, SO_ERROR, (char*)&soerr, &soerrsize))
            {
                int err = _GetError();
                traceprint("SOCKET[%p]: getsockopt() failed! Error %d: %s\n",
                    (void*)p.fd, err, _GetErrorStr(err).c_str());
                p.revents |= POLLERR; // hang up
            }
            else if (soerr)
            {
                traceprint("SOCKET[%p]: Delayed connect to failed! Error %d: %s\n",
                    (void*)p.fd, soerr, _GetErrorStr(soerr).c_str());
                p.revents |= POLLERR; // hang up
            }
            else
            {
                // connected!
                traceprint("SOCKET[%p]: Delayed connect successful!\n", (void*)p.fd);
                ss.flags |= JUSTCONNECTED;
            }
        }

        if (p.revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            sissocket_close(p.fd);
            PV[i] = PV.back();
            PV.pop_back();
            ss.flags |= CANDISCARD;
        }
        else
            ++i;

        if (p.revents & POLLIN)
            ss.flags |= CANREAD;
        else if (p.revents & POLLOUT)
            ss.flags |= CANWRITE;

        _ready.push_back(ss);
    }

    *n = _ready.size();
    return _ready.data();
}
