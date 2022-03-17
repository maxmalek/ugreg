#include "handler_ctrl.h"
#include <sstream>
#include <stdio.h>
#include "civetweb/civetweb.h"
#include "json_out.h"
#include "json_in.h"
#include "jsonstreamwrapper.h"
#include "treefunc.h"
#include "config.h"
#include "sisclient.h"
#include "responseformat.h"

#define BR "<br />\n"


CtrlHandler::CtrlHandler(const ClientList& clients, const char *prefix)
    : RequestHandler(prefix, "application/json"), clients(clients)
{
}

CtrlHandler::~CtrlHandler()
{
}

static void writeToStream(BufferedWriteStream & ws, VarCRef sub, const Request & r)
{
    writeJson(ws, sub, !!(r.flags & RQF_PRETTY));
}

// parse ?a=bla&b=0 to a Var
static size_t importQueryStrVars(VarRef v, const char *vars)
{
    char tmp[8 * 1024];
    const size_t tocopy = strlen(vars) + 1; // ensure to always include \0
    if (tocopy > sizeof(tmp))
        return false;
    memcpy(tmp, vars, tocopy);
    mg_header hd[MG_MAX_HEADERS];
    const int num = mg_split_form_urlencoded(tmp, hd, MG_MAX_HEADERS);
    if (num < 0)
        return 0;
    for (int i = 0; i < num; ++i)
        v[hd[i].name] = hd[i].value ? hd[i].value : "";
    return (size_t)num;
}

static int field_found(const char* key,
    const char* filename,
    char* path,
    size_t pathlen,
    void* user_data)
{
    DataTree& params = *(DataTree*)user_data;
    return MG_FORM_FIELD_STORAGE_GET;
}

static int field_get(const char* key,
    const char* value,
    size_t valuelen,
    void* user_data)
{
    DataTree& params = *(DataTree*)user_data;
    params.root()[key] = value;

    return MG_FORM_FIELD_HANDLE_NEXT;
}

// This is called from many threads at once.
// Avoid anything that changes the tree, and ensure proper read-locking!
int CtrlHandler::onRequest(BufferedWriteStream& dst, mg_connection* conn, const Request& rq) const
{
    const char *q = rq.query.c_str();

    if(q)
    {
        while(*q == '/')
            ++q;
        const char *action = strchr(q, '/');
        SISClient *cl = NULL;
        const size_t namelen = action ? action - q : strlen(q);
        for(size_t i = 0; i < clients.size(); ++i)
            if(!strncmp(clients[i]->getConfig().name.c_str(), q, namelen))
            {
                cl = clients[i];
                break;
            }
        if(!cl)
            return 404;
        if(cl->getState() < SISClient::IDLE)
        {
            mg_send_http_error(conn, 503, "Not yet connected to device, wait a little...");
            return 503;
        }

        if(action && *action)
            ++action; // skip '/'
        else
        {
            mg_send_http_redirect(conn, (std::string(q) + '/').c_str(), 301);
            return 301;
        }

        if(!(action && *action))
            action = "detail";

        // TODO: move this to common shared code
        DataTree params;
        const mg_request_info *info = mg_get_request_info(conn);

        if(info->content_length)
        {
            if(const char *content_type = mg_get_header(conn, "Content-Type"))
                if(!mg_strncasecmp(content_type, "application/json", 16))
                {
                    size_t todo = info->content_length, pos = 0;
                    std::vector<char> rd(todo);
                    while(todo)
                    {
                        int done = mg_read(conn, &rd[pos], todo);
                        if(done > 0)
                        {
                            todo -= done;
                            pos += done;
                        }
                    }
                    if(!loadJsonDestructive(params.root(), rd.data(), rd.size()))
                    {
                        mg_send_http_error(conn, 400, "Bad JSON");
                        return 400;
                    }
                    if(params.root().type() != Var::TYPE_MAP)
                    {
                        mg_send_http_error(conn, 400, "Expected JSON object, not %s", params.root().typestr());
                        return 400;
                    }
                }
        }

        mg_form_data_handler handleForm = { field_found, field_get, NULL, &params };
        mg_handle_form_request(conn, &handleForm);

        const char* vq = info->query_string;

        const Var *vp = NULL;
        if(params.root().v->size() || (vq && importQueryStrVars(params.root(), vq))) // only try to pass vars to Lua when we actually get some vars in the query string
            vp = params.root().v;

        // TODO make expiry configurable?
        SISClient::ActionResult res = cl->queryAsync(action, VarCRef(params, vp), cl->getConfig().device.getHttpTimeout()).get();
        if(res.error || res.status >= 400)
        {
            if(!res.status)
                res.status = 500;
            mg_send_http_error(conn, res.status, "%s", res.text.c_str());
            return res.status;
        }

        mg_send_http_ok(conn, res.contentType.length() ? res.contentType.c_str() : "text/plain; charset=utf-8", res.text.length());
        mg_write(conn, res.text.c_str(), res.text.length());
        return res.status ? res.status : 200;
    }

    return 404;
}
