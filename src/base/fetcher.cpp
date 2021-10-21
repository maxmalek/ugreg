#include "fetcher.h"
#include "subprocess.h"
#include "env.h"
#include <sstream>
#include <string>
#include <vector>
#include "view/viewexec.h"
#include "view/viewparser.h"
#include "pathiter.h"
#include "util.h"

Fetcher::Fetcher()
    : _useEnv(false)
{
}

Fetcher::~Fetcher()
{
}

bool Fetcher::init(VarCRef config)
{
    _prepareEnv(config);

    if(!_doStartupCheck(config))
        return false;

    return true;
}

bool Fetcher::_doStartupCheck(VarCRef config) const
{
    if (VarCRef check = config.lookup("startup-check"))
    {
        subprocess_s proc;
            
        if (!_createProcess(proc, check, subprocess_option_combined_stdout_stderr))
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
        int ok = subprocess_join(&proc, &ret);
        subprocess_destroy(&proc);

        if (!ok)
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
                os << it.key() << '=' << s;
                _env.push_back(os.str());
            }
    }
}

bool Fetcher::_fetch(VarCRef launch, const char* path) const
{
    view::VM vm;

    vm.makeVar("0", 1) = path; // $0 is the full path

    char buf[32];
    size_t num = 0;
    // $1 becomes first path fragment, and so on
    for(PathIter it(path); it.hasNext(); ++it)
    {
        ++num;
        const char *ns = sizetostr_unsafe(buf, sizeof(buf), num);
        vm.makeVar(ns, strlen(ns)) = it.value().s;
    }

    //Var out = vm.fromTemplate(launch);
    

    return false;
}

bool Fetcher::_createProcess(subprocess_s& proc, VarCRef launch, int options) const
{
    const Var *a = launch.v->array();
    if(!a)
        return false;
    const size_t n = launch.size();
    const char** pcmd = (const char**)alloca((n + 1) * sizeof(const char*));
    for (size_t i = 0; i < n; ++i)
        pcmd[i] = a[i].asCString(*launch.mem);
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
    else // just clone whatever env we have if there are no custom options set
        options |= subprocess_option_inherit_environment;

    if (subprocess_create_ex(pcmd, options, penv, &proc))
    {
        return false;
    }
    return true;
}
