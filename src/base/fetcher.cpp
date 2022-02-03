#include "fetcher.h"
#include "subprocess.h"
#include "env.h"
#include <sstream>
#include <string>
#include <vector>
#include "pathiter.h"
#include "util.h"
#include "subproc.h"
#include "datatree.h"

#include "json_out.h"

Fetcher::Fetcher()
    : _useEnv(false), validity(0)
    , fetchsingle(*this), fetchall(*this), postall(*this), postsingle(*this)
{
}

Fetcher::~Fetcher()
{
    alldata.clear(*this);
}

Fetcher* Fetcher::New(VarCRef config)
{
    Fetcher* f = new Fetcher;
    if(f && !f->init(config))
    {
        f->destroy();
        f = NULL;
    }
    return f;
}

void Fetcher::destroy()
{
    delete this;
}

bool Fetcher::init(VarCRef config)
{
    if(VarCRef validityref = config.lookup("validity"))
    {
        if(const char *s = validityref.asCString())
        {
            if(!strToDurationMS_NN(&validity, s).ok())
            {
                printf("validity: invalid duration: '%s'\n", s);
                return false;
            }
        }
        else
        {
            printf("validity: expected duration string, got %s", validityref.typestr());
            return false;
        }

    }

    _prepareEnv(config);

    if(!_doStartupCheck(config))
    {
        printf("FATAL: Startup check failed. Exiting.\n");
        exit(1);
        return false;
    }

    if(!_prepareView(fetchall, config, "fetch-all"))
        return false;

    if(!_prepareView(fetchsingle, config, "fetch-single"))
        return false;

    if (!_prepareView(postall, config, "post-all"))
        return false;

    if (!_prepareView(postsingle, config, "post-single"))
        return false;

    _config = config;
    return true;
}

bool Fetcher::_doStartupCheck(VarCRef config) const
{
    if (VarCRef check = config.lookup("startup-check"))
    {
        subprocess_s proc;

        if (!_createProcess(&proc, check, subprocess_option_combined_stdout_stderr))
        {
            printf("Fetcher init (startup-check): Failed to create subprocess\n");
            return false;
        }

        // display subproc stdout
        {
            FILE* pout = subprocess_stdout(&proc);
            size_t bytes;
            char buf[256];
            // TODO: could change output color here or something
            do
            {
                bytes = fread(buf, 1, sizeof(buf), pout);
                fwrite(buf, 1, bytes, stdout);
            }
            while (bytes);
        }


        int ret = 0;
        int err = subprocess_join(&proc, &ret);
        subprocess_destroy(&proc);

        if (err)
        {
            printf("Fetcher init (startup-check): Failed subprocess_join()\n");
            return false;
        }

        if (ret)
        {
            printf("Fetcher init (startup-check): Failed with return code %d\n", ret);
            return false;
        }

        printf("Fetcher init (startup-check): Check passed\n");
    }

    return true;
}

Var Fetcher::fetchOne(const char *suffix, size_t len)
{
    Var ret;

    if(fetchsingle.loaded())
        ret = _fetch(fetchsingle, suffix, len);
    else
    {
        // Use fetch-all to fetch everything and then extract a single element
        // TODO: check if valid/expired

        StrRef k = this->lookup(suffix, len); // will be 0 if not yet known string
        if (Var* v = alldata.lookupNoFetch(k))
            ret = std::move(*v);
        else
        {
            alldata = _fetchAllNoPost();
            if (!k)
                k = this->lookup(suffix, len); // at this point the string is pooled if it exists
            if (Var* v = alldata.lookupNoFetch(k))
                ret = std::move(*v);
        }
    }

    if (postsingle.loaded())
        ret = _postproc(postsingle, std::move(ret));

    return ret;
}

Var Fetcher::fetchAll()
{
    if (!postsingle.loaded())
        return _fetchAllNoPost();

    // Apply post-single to all individual values (at this point we know it's a map)
    Var all = std::move(_fetchAllNoPost());
    if (Var::Map* m = all.map())
        for (Var::Map::MutIterator it = m->begin(); it != m->end(); ++it)
             it.value()= _postproc(postsingle, std::move(it.value()));

    return all;
}

Var Fetcher::_fetchAllNoPost()
{
    alldata.clear(*this);
    Var ret = _fetch(fetchall, NULL, 0);

    if(!ret.isNull() && postall.loaded())
    {
        const char* oldtype = ret.typestr();
        ret = _postproc(postall, std::move(ret));
        if(ret.type() != Var::TYPE_MAP)
            printf("Fetcher::fetchAll: Was %s before postproc, is now %s", oldtype, ret.typestr());
    }

    if(!ret.isNull() && ret.type() != Var::TYPE_MAP)
    {
        printf("Fetcher::fetchAll: Expected map but got %s. This is an error.\n", ret.typestr());
        ret.clear(*this);
    }
    return ret;
}

Var Fetcher::_postproc(const view::View& vw, Var&& var)
{
    assert(vw.loaded());
    Var tmp = std::move(var);
    Var res = vw.produceResult(*this, VarCRef(this, &tmp), _config);
    tmp.clear(*this);
    return res;
}

void Fetcher::_prepareEnv(VarCRef config)
{
    VarCRef env = config.lookup("env");

    _env.clear();
    // FIXME: this needs better rules when to use env
    _useEnv = env && env.type() == Var::TYPE_MAP && !env.v->map()->empty();
    if (_useEnv)
    {
        // Get all current env vars as a starting point...
        _env = enumerateEnvVars();

        // ... and append all extra ones
        const Var::Map* m = env.v->map();
        for (Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
            if (const char* s = it.value().asCString(*env.mem))
            {
                std::ostringstream os;
                os << env.mem->getS(it.key()) << '=' << s;
                _env.push_back(os.str());
            }
    }
}

bool Fetcher::_prepareView(view::View& vw, VarCRef config, const char* key)
{
    if (VarCRef v = config.lookup(key))
    {
        if (vw.load(v, true))
            printf("Loaded view for %s (%u ops)\n", key, (unsigned)vw.exe.cmds.size());
        else
        {
            printf("Failed to load view for %s\n", key);
            return false;
        }
    }

    return true;
}

Var Fetcher::_fetch(const view::View& vw, const char* path, size_t len)
{
    bool ok = false;

    Var vars;
    VarRef vmvars(this, &vars);

    if(path)
    {
        vmvars["0"].setStr(path, len); // $0 is the full path

        // TODO: should this ever be extended to be called with more than a single subdir, pass the individual path fragments along
        assert(!strchr(path, '/'));
        /*
        char buf[32];
        size_t num = 0;
        // $1 becomes first path fragment, and so on
        for(PathIter it(path); it.hasNext(); ++it)
        {
            ++num;
            const char *ns = sizetostr_unsafe(buf, sizeof(buf), num);
            printf("$%s = %s\n", ns, it.value().s);
            vmvars[ns] = it.value().s;
        }
        */
    }

    Var params = vw.produceResult(*this, _config, vmvars);
    vars.clear(*this); // no longer needed after this
    printf("FETCH EXEC (path: %s): %s\n", path, dumpjson(VarCRef(this, &params)).c_str());

    Var ret;

    // params should be an array of strings at this point. This will fail if it's not.
    subprocess_s proc;
    ok =_createProcess(&proc, VarCRef(this, &params), subprocess_option_enable_async | subprocess_option_no_window);
    if(ok)
    {
        const char *procname = NULL;
        if(Var *a = params.array())
            procname = a[0].asCString(*this);

        ok = loadJsonFromProcess(VarRef(this, &ret), &proc, procname);

        subprocess_destroy(&proc);

        //printf("FETCH RESULT:\n-------\n%s\n---------\n", dumpjson(VarCRef(this, &ret), true).c_str());
    }
    else
    {
        printf("launch failed!\n");
        ret.clear(*this);
    }

    params.clear(*this);

    return ret;
}

bool Fetcher::_createProcess(subprocess_s *proc, VarCRef launch, int options) const
{
    const Var *a = launch.v->array();
    if(!a)
        return false;
    const size_t n = launch.size();
    const char** pcmd = (const char**)alloca((n + 1) * sizeof(const char*));
    for (size_t i = 0; i < n; ++i)
    {
        const char *s = a[i].asCString(*launch.mem);
        if(!s)
            return false;
        pcmd[i] = s;
    }
    pcmd[n] = NULL;

    options |= subprocess_option_no_window;

    const char** penv = NULL;
    if (_useEnv)
    {
        penv = (const char**)alloca((_env.size() + 1) * sizeof(const char*));
        for (size_t i = 0; i < _env.size(); ++i)
            penv[i] = _env[i].c_str();
        penv[_env.size()] = NULL;
    }

    return createProcess(proc, pcmd, penv, options);
}
