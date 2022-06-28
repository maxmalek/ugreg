#include "mxresolv.h"
#include <sstream>
#include <string>
#include <algorithm>
#include <string.h>
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

void MxResolvResult::parse(const char* s)
{
    const char* colon = strrchr(s, ':');
    if (colon)
    {
        port = atoi(colon + 1);
        host.assign(s, colon);
    }
    else
        host = s;

    priority = -1; // .well-known should be preferred (for SRV records this is >= 0)
    weight = 1;
}

bool MxResolvResult::operator<(const MxResolvResult& o) const
{
    return priority < o.priority // lower priority wins
        || (priority == o.priority && weight > o.weight); // higher weight wins
}

bool MxResolvResult::validate()
{
    if(!port)
        port = 8448; // default matrix port
    return !host.empty();
}

// FIXME: finish this.
static void srvLookup(MxResolvList &dst, const char *host)
{
    const size_t oldsize = dst.size();
    std::string lookup = "_matrix._tcp.";
    lookup += host;
    printf("MxResolv/SRV: Looking up %s ...\n", lookup.c_str());

#ifdef _WIN32
    PDNS_RECORD prec = NULL;
    DNS_STATUS dns = DnsQuery_A(lookup.c_str(), DNS_TYPE_SRV, DNS_QUERY_STANDARD, NULL, &prec, NULL);
    if(dns == 0)
    {
        for(PDNS_RECORD p = prec; p; p = p->pNext)
        {
            printf("DNS: %s (type: %u)\n", p->pName, p->wType);
            if(p->wType == DNS_TYPE_SRV)
            {
                printf("SRV: %s:%u (prio=%d, weight=%d)\n",
                    p->Data.Srv.pNameTarget, p->Data.Srv.wPort, p->Data.Srv.wPriority, p->Data.Srv.wWeight);
                MxResolvResult res;
                res.host = p->Data.Srv.pNameTarget;
                res.port = p->Data.Srv.wPort;
                res.priority = p->Data.Srv.wPriority;
                res.weight = p->Data.Srv.wWeight;
                // TODO: handle TTL?
                if(res.validate())
                    dst.push_back(res);
            }
        }
    }
    else
        printf("srvLookup: Failed for '%s', error = %d\n", host, dns);

    if(prec)
        DnsRecordListFree(prec, DnsFreeRecordListDeep);

#else // unix

    struct __res_state rs;
    int rc = res_ninit(&rs);
    if (rc < 0)
    {
        printf("srvLookup: res_ninit() failed, error = %d\n", rc);
        return;
    }
    //printf("rs.options = %d\n", rs.options);
    unsigned char nsbuf[8*1024] = {0};
    int len = res_nquery(&rs, lookup.c_str(), ns_c_in, ns_t_srv, nsbuf, sizeof(nsbuf));
    if(len < 0)
    {
        perror("ERROR: res_nquery");
        return;
    }
    //printf("(len=%d)---nsbuf---:%s\n", len, nsbuf);

    ns_msg msg;
    ns_rr rr;

    ns_initparse(nsbuf, len, &msg);
    int N = ns_msg_count(msg, ns_s_an);

    for(int i = 0; i < N; ++i)
    {
        ns_parserr(&msg, ns_s_an, i, &rr);

        /*char dispbuf[1024];
        ns_sprintrr(&msg, &rr, NULL, NULL, dispbuf, sizeof(dispbuf));
        printf("\t%s \n", dispbuf);*/

        ns_type type = ns_rr_type(rr);
        if(type == ns_t_srv)
        {
            if(ns_rr_rdlen(rr) < 3U * NS_INT16SZ)
                continue;
            const char *name = (const char *)ns_rr_name(rr);
            const unsigned char *rdata = ns_rr_rdata(rr);
            int priority = ns_get16(rdata);  rdata += NS_INT16SZ;
            int weight = ns_get16(rdata);  rdata += NS_INT16SZ;
            unsigned port = ns_get16(rdata);  rdata += NS_INT16SZ;

            printf("SRV: %s:%u (prio=%d, weight=%d)\n",
                name, port, priority, weight);

            MxResolvResult res;
            res.host = name;
            res.port = port;
            res.priority = priority;
            res.weight = weight;
            // TODO: handle TTL?
            if (res.validate())
                dst.push_back(res);
        }
    }
#endif

    // sort added entries by priority and weight (keep existing entries as-is!)
    if(dst.size() != oldsize)
        std::sort(dst.begin() + oldsize, dst.end());
}

MxResolvList lookupHomeserverForHost(const char* host, u64 timeoutMS, size_t maxsize)
{
    MxResolvList ret;

    // try .well-known first
    const char *what = "/.well-known/matrix/server";

    printf("MxResolv/GET: Looking up %s%s ...\n", host, what);

    DataTree tmp(DataTree::TINY);
    MxGetJsonResult err = mxGetJson(tmp.root(), host, 443, what, (int)timeoutMS, maxsize);
    if(err == MXGJ_OK)
    {
        MxResolvResult res;
        res.port = 0;
        // root is now known to be a map with at least 1 element
        if(VarCRef ref = tmp.root().lookup("m.server"))
        {
            if(const char *s = ref.asCString())
            {
                res.parse(s);
                if(res.validate())
                {
                    printf("MxResolv/GET: Got %s:%u\n", res.host.c_str(), res.port);
                    ret.push_back(res);
                }
            }
        }
    }

    srvLookup(ret, host);

    return ret;
}
