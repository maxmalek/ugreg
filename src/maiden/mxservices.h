#pragma once

#include <map>
#include <vector>
#include "webserver.h"
#include "datatree.h"
#include "mxstore.h"
#include "mxsearch.h"
#include "webstuff.h"

class MxSources;


class MxWellknownHandler : public RequestHandler
{
public:
    MxWellknownHandler();
    bool init(VarCRef cfg);
    virtual int onRequest(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const override;

private:
    std::map<std::string, std::string> data;
};

class MxSearchHandler : public RequestHandler
{
public:
    MxSearchHandler(MxSources& sources);
    virtual ~MxSearchHandler();

    bool init(VarCRef cfg);
    virtual int onRequest(BufferedWriteStream& dst, struct mg_connection* conn, const Request& rq) const override;
    bool checkHS;
    bool askHS;
    bool overrideAvatar;
    bool overrideDisplayname;
    MxSearchConfig searchcfg;
    MxSearch search;

    struct ServerConfig
    {
        URLTarget target;
        int timeout;
        std::string authToken;
    };

    ServerConfig homeserver;
    std::vector<ServerConfig> otherServers;

    struct AccessKeyConfig
    {
        bool enabled;
    };

    typedef std::map<std::string, AccessKeyConfig> AccessKeyMap;
    AccessKeyMap accessKeys;

private:
    struct MxSearchResultsEx
    {
        MxSearchResults results;
        bool limited = false;
        int errcode = 0;
        std::string errstr;
    };
    MxSearchResultsEx doSearch(const char* term, size_t limit, const VarCRef hsResultsArray) const;
    void translateResults(VarRef dst, const MxSearchResultsEx& results) const;
    MxSearchResults mergeResults(const MxSearchResults& myresults, const MxSearchResults& hsresults) const;
    static void _ApplyElementHack(MxSearchResults& results, const std::string& term);
    const AccessKeyConfig *checkAccessKey(const std::string& token) const;

    static MxSearchResultsEx QueryOneServer(const ServerConfig& sv, const std::string& query, VarCRef requestVars);

    MxSources& _sources;
};

