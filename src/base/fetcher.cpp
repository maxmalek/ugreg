#include "fetcher.h"
#include "subprocess.h"
#include "env.h"
#include <sstream>
#include <string>
#include <vector>
#include "viewexec.h"
#include "viewparser.h"
#include "pathiter.h"
#include "util.h"
#include "subproc.h"
#include "datatree.h"

#include "json_out.h"

Fetcher::Fetcher(TreeMem& mem)
    : _useEnv(false), pathparts(0), validity(0), fetchsingle(mem), fetchall(mem)
{
}

Fetcher::~Fetcher()
{
}

Fetcher* Fetcher::New(TreeMem& mem, VarCRef config)
{
    Fetcher *f = (Fetcher*)mem.Alloc(sizeof(Fetcher));
    if(f && !f->init(config))
    {
        f->destroy();
        f = NULL;
    }
    return f;
}

void Fetcher::destroy()
{
    TreeMem * const pmem = fetchsingle.exe.mem; // this happens to store our mem
    this->~Fetcher();
    pmem->Free(this, sizeof(*this));
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

    }

    printf("Fetcher init (startup-check): Check passed\n");
    return true;
}

Var Fetcher::fetchOne(TreeMem& mem, VarCRef spec) const
{
    return Var();
}

Var Fetcher::fetchAll(TreeMem& mem, VarCRef spec) const
{
    return Var();
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

bool Fetcher::_fetch(TreeMem& dst, VarCRef launch, const char* path) const
{
    TreeMem mem;
    view::VM vm(mem);

    vm.makeVar("0", 1) = path; // $0 is the full path

    char buf[32];
    size_t num = 0;
    // $1 becomes first path fragment, and so on
    for(PathIter it(path); it.hasNext(); ++it)
    {
        ++num;
        const char *ns = sizetostr_unsafe(buf, sizeof(buf), num);
        printf("$%s = %s\n", ns, it.value().s); 
        vm.makeVar(ns, strlen(ns)) = it.value().s;
    }

    bool ok = false;
    Var params = fetchsingle.produceResult(dst, launch, VarCRef()); // FIXME

    // params should be an array of strings at this point. This will fail if it's not.
    subprocess_s proc;
    ok =_createProcess(&proc, VarCRef(mem, &params), subprocess_option_enable_async | subprocess_option_no_window);
    if(ok)
    {
        const char *procname = NULL;
        if(Var *a = params.array())
            procname = a[0].asCString(mem);

        DataTree tree;
        bool ok = loadJsonFromProcess(&tree, &proc, procname);

        subprocess_destroy(&proc);

        printf("FETCH RESULT:\n-------\n%s\n---------\n", dumpjson(tree.root(), true).c_str());
    }
    else
        printf("launch failed!\n");

    params.clear(dst);


    
    // TODO MERGE

    /*{
        std::unique_lock
    }*/

    return ok;
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
