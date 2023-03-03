#pragma once

#include <map>
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
    int hsTimeout;
    MxSearchConfig searchcfg;
    MxSearch search;
    URLTarget homeserver;

private:
    struct MxSearchResultsEx
    {
        MxSearchResults results;
        bool limited;
    };
    MxSearchResultsEx doSearch(const char* term, size_t limit, const VarCRef hsResultsArray) const;
    void translateResults(VarRef dst, const MxSearchResultsEx& results) const;
    MxSearchResults mergeResults(const MxSearchResults& myresults, const MxSearchResults& hsresults) const;
    static void _ApplyElementHack(MxSearchResults& results, const std::string& term);

    MxSources& _sources;
};

