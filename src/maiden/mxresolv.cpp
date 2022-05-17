#include "mxresolv.h"
#include <civetweb/civetweb.h>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#include <WinDNS.h>
#else
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#endif

static std::string srvLookup(const char *host)
{
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

    return "";
}

int lookupHomeserverForHost(std::string& dst, const char* host, u64 timeoutMS, size_t maxsize)
{
    // try .well-known first
    const char *what = "/.well-known/matrix/server";
    char errbuf[1024];
    int res = 0;
    std::ostringstream os;
    //os << "GET " << what << " HTTP/1.1\r\n"
    os << "GET / HTTP/1.1\r\n"
        << "Host: " << host << "\r\n"
        << "Connection: close\r\n"
        << "\r\n";

    struct mg_client_options opt = { 0 };
    opt.host = host;
    opt.port = 443;           /* Default HTTPS port */
    opt.client_cert = NULL;   /* Client certificate, if required */
    opt.server_cert = NULL;   /* Server certificate to verify */
    opt.host_name = opt.host; /* Host name for SNI */

    if (mg_connection* c = mg_connect_client_secure(&opt, errbuf, sizeof(errbuf)))
    {
        std::string request = os.str();
        mg_write(c, request.c_str(), request.size());

        res = mg_get_response(c, errbuf, sizeof(errbuf), -1); // TODO: timeout
        if(res == 200) // FIXME: does this handle redirects?
        {
            for(;;)
            {
                int rd = mg_read(c, errbuf, sizeof(errbuf));
                if(rd == 0)
                    break;
                if(rd > 0)
                {
                    dst.append(errbuf, rd);
                    if(maxsize >= rd)
                        maxsize -= rd;
                    else
                        break;
                }
            }
        }
        mg_close_connection(c);
    }

    std::string srv = srvLookup(host);


    return 0;
}
