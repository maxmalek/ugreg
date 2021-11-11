#include "subproc.h"
#include <thread>
#include <sstream>

#include "subprocess.h"
#include "pmalloca.h"

#include <assert.h>
#include "datatree.h"
#include "json_in.h"

static void procfail(ProcessReadStream& ps, const char *procname)
{
    size_t pos = ps.Tell();
    unsigned i = 0;
    char c;
    std::ostringstream os;
    while(((c = ps.Take())) && i++ < 100)
        os << c;
    if(!pos && !i)
        printf("[%s] Did not produce output before it died\n", procname);
    else
        printf("[%s] JSON parse error after reading %u bytes, before:\n%s\n",
            procname, unsigned(pos), os.str().c_str());
}

bool createProcess(subprocess_s* proc, const char** args, const char** env, int options)
{
    if (!env)
        options |= subprocess_option_inherit_environment;

    int err = subprocess_create_ex(args, options, env, proc);

#ifdef _WIN32
    if(err)
    {
        // Aside from .exe, .bat is the only natively executable file on windows.
        // So to ease testing, we support using .bat stubs to start the actual scripts.
        const char* const oldarg0 = args[0];
        std::string arg0 = args[0];
        arg0 += ".bat";
        args[0] = arg0.c_str();
        err = subprocess_create_ex(args, options, env, proc);
        args[0] = oldarg0;
    }
#endif

    if (err)
        printf("Failed to create process [%s]\n", args[0]);

    return err == 0;
}

bool loadJsonFromProcess(DataTree *tree, const char** args, const char **env)
{
    subprocess_s proc;
    if(!createProcess(&proc, args, env, subprocess_option_enable_async | subprocess_option_no_window))
        return false;
    
    bool ok = loadJsonFromProcess(tree, &proc, args[0]);
    subprocess_destroy(&proc);
    return ok;
}

bool loadJsonFromProcess(DataTree* tree, subprocess_s* proc, const char* procname)
{
    char buf[12*1024];
    ProcessReadStream ps(proc, ProcessReadStream::DONTTOUCH, &buf[0], sizeof(buf));

    bool ok = loadJsonDestructive(tree->root(), ps);

    if(ok)
    {
        printf("[%s] parsed as json, waiting until it exits...\n", procname);
    }
    else
    {
        procfail(ps, procname);
        if(subprocess_alive(proc))
        {
            printf("[%s] failed to parse and still alive, killing\n", procname);
            subprocess_terminate(proc);
        }
    }

    int ret = 0;
    subprocess_join(proc, &ret);
    ok = !ret; // if the process reports failure, don't use it even if it's valid json
    printf("[%s] exited with code %d\n", procname, ret);
    if(!ok && subprocess_stderr(proc))
    {
        bool hdr = false;
        for(;;) // attempt to output stderr of failed process
        {
            char buf[1024];
            unsigned rd = subprocess_read_stderr(proc, buf, sizeof(buf));
            if(!rd)
                break;
            if(!hdr)
            {
                printf("---- [%s] begin stderr dump ----\n", procname);
                hdr = true;
            }
            fwrite(buf, 1, sizeof(buf), stdout);
        }
        if(hdr)
            printf("---- [%s] end stderr dump ----\n", procname);
    }

    return ok;
}

DataTree * loadJsonFromProcessSync(AsyncLaunchConfig&& cfg)
{
    const size_t sz = (cfg.args.size() + 1) * sizeof(const char*);
    const char ** args = (const char **)_malloca(sz);
    if(!args)
        return NULL;
    size_t i;
    for(i = 0; i < cfg.args.size(); ++i)
        args[i] = cfg.args[i].c_str();
    args[i] = NULL; // terminator
    
    DataTree *tree = new DataTree;
    bool ok = loadJsonFromProcess(tree, args, NULL);
    if(!ok)
    {
        delete tree;
        tree = NULL;
    }
    _freea(args);
    return tree;
}

std::future<DataTree*> loadJsonFromProcessAsync(AsyncLaunchConfig&& cfg)
{
    return std::async(std::launch::async, loadJsonFromProcessSync, std::move(cfg));
}


ProcessReadStream::ProcessReadStream(subprocess_s* proc, CloseBehavior close, char* buf, size_t bufsz)
    : ProcessReadStream(NULL, _Read, proc, close, buf, bufsz)
{
}

ProcessReadStream::ProcessReadStream(InitFunc initf, ReadFunc readf, subprocess_s* proc, CloseBehavior close, char* buf, size_t bufsz)
    : BufferedReadStream(initf, readf, buf, bufsz), _proc(proc), closeb(close)
{
    assert(proc);
}

ProcessReadStream::~ProcessReadStream()
{
    switch(closeb)
    {
        case DONTTOUCH: break;
        case WAIT: subprocess_join(_proc, NULL); break;
        case TERMINATE: subprocess_terminate(_proc); break;
    }
}

int ProcessReadStream::wait()
{
    int ret = 0;
    subprocess_join(_proc, &ret);
    closeb = DONTTOUCH;
    return ret;
}

void ProcessReadStream::terminate()
{
    subprocess_terminate(_proc);
    closeb = DONTTOUCH;
}

size_t ProcessReadStream::_Read(void* dst, size_t bytes, BufferedReadStream* self)
{
    ProcessReadStream* me = static_cast<ProcessReadStream*>(self);
    return subprocess_read_stdout(me->_proc, (char*)dst, bytes);
}
