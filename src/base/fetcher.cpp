#include "fetcher.h"
#include "subprocess.h"
#include <sstream>
#include <string>
#include <vector>

Fetcher::Fetcher()
{
}

Fetcher::~Fetcher()
{
    _env.clear(*this);
}

bool Fetcher::init(VarCRef config)
{
    VarCRef env = config.lookup("env");
    if(VarCRef check = config.lookup("startup-check"))
        if(const Var *a = check.v->array())
        {
            const size_t n = check.size();
            const char ** cmd = (const char **)alloca((n + 1) * sizeof(const char *));
            for(size_t i = 0; i < n; ++i)
                cmd[i] = a[i].asCString(*check.mem);
            cmd[n] = NULL;

            const char ** penv = NULL;
            if(env && env.type() == Var::TYPE_MAP)
            {
                std::vector<std::string> tmp;
                const Var::Map *m = env.v->map();
                for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
                    if(const char *s = it.value().asCString(*env.mem))
                    {
                        std::ostringstream os;
                        os << it.key() << '=' << s;
                        tmp.push_back(os.str());
                    }

                penv = (const char**)alloca((tmp.size() + 1) * sizeof(const char*));
                for (size_t i = 0; i < tmp.size(); ++i)
                    penv[i] = tmp[i].c_str();
                penv[tmp.size()] = NULL;
            }

            subprocess_s proc;
            if(subprocess_create_ex(cmd, subprocess_option_combined_stdout_stderr | subprocess_option_no_window, penv, &proc))
            {
                printf("Fetcher init (startup-check): Failed to create subprocess\n");
                return false;
            }

            {
                FILE *pout = subprocess_stdout(&proc);
                size_t bytes;
                char buf[256];
                do
                {
                    bytes = fread(buf, 1, sizeof(buf), pout);
                    fwrite(buf, 1, bytes, stdout);
                }
                while(bytes);
            }
            

            int ret = 0;
            if(subprocess_join(&proc, &ret))
            {
                printf("Fetcher init (startup-check): Failed subprocess_join()\n");
                return false;
            }

            if(ret)
            {
                printf("Fetcher init (startup-check): Failed with return code %d\n", ret);
                return false;
            }

        }

    return false;
}

Var Fetcher::fetchOne(TreeMem& mem, VarCRef spec) const
{
    return Var();
}

Var Fetcher::fetchAll(TreeMem& mem, VarCRef spec) const
{
    return Var();
}
