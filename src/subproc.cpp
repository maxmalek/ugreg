#include "subproc.h"
#include <thread>
#include <sstream>

#include "subprocess.h"
#include "pmalloca.h"

#include <assert.h>
#include "datatree.h"
#include "json_in.h"

static void procfail(ProcessReadStream& ps, const char **args)
{
    size_t pos = ps.Tell();
    unsigned i = 0;
    char c;
    std::ostringstream os;
    while(((c = ps.Take())) && i++ < 100)
        os << c;
    if(!pos && !i)
        printf("[%s] Did not produce output before it died\n", args[0]);
    else
        printf("[%s] JSON parse error after reading %u bytes, before:\n%s\n",
            args[0], unsigned(pos), os.str().c_str());

}

bool loadJsonFromProcess(DataTree *tree, const char** args)
{
    // Aside from .exe, .bat is the only natively executable file on windows.
    // So to ease testing, we support using .bat stubs to start the actual scripts.
#ifdef _WIN32
    const char * const oldarg0 = args[0];
    std::string arg0 = args[0];
    arg0 += ".bat";
    args[0] = arg0.c_str();
#endif

    subprocess_s proc;
    int err = subprocess_create(args, subprocess_option_enable_async | subprocess_option_no_window | subprocess_option_inherit_environment, &proc);

#ifdef _WIN32
    args[0] = oldarg0;
#endif

    if (err)
    {
        printf("Failed to create process [%s]\n", args[0]);
        return false;
    }

    sleepMS(1000);

    char buf[12*1024];
    ProcessReadStream ps(&proc, ProcessReadStream::DONTTOUCH, &buf[0], sizeof(buf));

    bool ok = loadJsonDestructive(tree->root(), ps);

    if(ok)
    {
        printf("[%s] parsed as json, waiting until it exits...\n", args[0]);
    }
    else
    {
        procfail(ps, args);
        if(subprocess_alive(&proc))
        {
            printf("[%s] failed to parse and still alive, killing\n", args[0]);
            subprocess_terminate(&proc);
        }
    }

    int ret = 0;
    subprocess_join(&proc, &ret);
    ok = !ret; // if the process reports failure, don't use it even if it's valid json
    printf("[%s] exited with code %d\n", args[0], ret);
    if(!ok && subprocess_stderr(&proc))
    {
        bool hdr = false;
        for(;;) // attempt to output stderr of failed process
        {
            char buf[1024];
            unsigned rd = subprocess_read_stderr(&proc, buf, sizeof(buf));
            if(!rd)
                break;
            if(!hdr)
            {
                printf("---- [%s] begin stderr dump ----\n", args[0]);
                hdr = true;
            }
            fwrite(buf, 1, sizeof(buf), stdout);
        }
        if(hdr)
            printf("---- [%s] end stderr dump ----\n", args[0]);
    }

    subprocess_destroy(&proc);

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
    bool ok = loadJsonFromProcess(tree, args);
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
