#include "mxresolv.h"
#include <sstream>
#include <string>
#include "request.h"
#include "datatree.h"
#include "mxhttprequest.h"

#ifdef _WIN32
#include <Windows.h>
#include <WinDNS.h>
#else
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#endif

// FIXME: finish this.
static MxResolvResult srvLookup(const char *host)
{
    MxResolvResult ret;
    ret.port = 0;

#ifdef _WIN32
    PDNS_RECORD prec = NULL;
    DNS_STATUS dns = DnsQuery_A(host, DNS_TYPE_SRV, DNS_QUERY_STANDARD, NULL, &prec, NULL);
    if(dns == 0)
    {
        for(PDNS_RECORD p = prec; p; p = p->pNext)
        {
            printf("DNS: %s (type: %u)\n", p->pName, p->wType);
            if(p->wType == DNS_TYPE_SRV)
            {
                printf("SRV: %s:%u\n", p->Data.Srv.pNameTarget, p->Data.Srv.wPort);
            }
        }
    }
    else
        printf("srcLookup: Failed for '%s', error = %d", host, dns);

    if(prec)
        DnsRecordListFree(prec, DnsFreeRecordListDeep);

#else // unix
#error WRITE ME

#endif

    return ret;
}

MxResolvResult lookupHomeserverForHost(const char* host, u64 timeoutMS, size_t maxsize)
{
    MxResolvResult ret;
    ret.port = 0;

    // try .well-known first
    const char *what = "/.well-known/matrix/server";

    DataTree tmp(DataTree::TINY);
    int n = mxGetJson(tmp.root(), host, 443, what, (int)timeoutMS, maxsize);
    if(n > 0)
    {
        // root is now known to be a map with at least 1 element
        if(VarCRef ref = tmp.root().lookup("m.server"))
        {
            if(const char *s = ref.asCString())
            {
                const char *colon = strrchr(s, ':');
                if(colon)
                {
                    ret.port = atoi(colon + 1);
                    ret.host.assign(s, colon);
                }
                else
                    ret.host = s;
            }
        }
    }

    if(ret.host.empty())
        ret = srvLookup(host);

    if(!ret.host.empty() && !ret.port)
        ret.port = 8448; // default matrix port

    return ret;
}
