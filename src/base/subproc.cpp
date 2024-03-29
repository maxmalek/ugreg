#include "subproc.h"
#include <thread>
#include <sstream>

#include "subprocess.h"
#include "pmalloca.h"

#include <assert.h>
#include "datatree.h"
#include "serialize.h"
#include "util.h"

static void procfail(ProcessReadStream& ps, const char *procname)
{
    size_t pos = ps.Tell();
    unsigned i = 0;
    char c;
    std::ostringstream os;
    while(((c = ps.Take())) && i++ < 100)
        os << c;
    if(!pos && !i)
        logerror("[%s] Did not produce output before it died", procname);
    else
        logerror("[%s] JSON parse error after reading %u bytes, before:\n%s",
            procname, unsigned(pos), os.str().c_str());
}

bool createProcess(subprocess_s* proc, const char* const * args, const char* const * env, int options)
{
    if(!args || !args[0])
        return false;

    if (!env)
        options |= subprocess_option_inherit_environment;

    int err = subprocess_create_ex(args, options, env, proc);

#ifdef _WIN32
    if(err)
    {
        // Aside from .exe, .bat is the only natively executable file on windows.
        // So to ease testing, we support using .bat stubs to start the actual scripts.
        std::vector<const char*> winargs;
        for(const char * const *p = args; *p; ++p)
            winargs.push_back(*p);
        winargs.push_back(NULL);
        std::string arg0 = args[0];
        for(size_t i = 0; i < arg0.length(); ++i) // .bat is too dumb to work with '/' as path separator
            if(arg0[i] == '/')
                arg0[i] = '\\';
        arg0 += ".bat";
        winargs[0] = arg0.c_str();
        err = subprocess_create_ex(&winargs[0], options, env, proc);
    }
#endif

    if (err)
        logerror("Failed to create process [%s]", args[0]);

    return err == 0;
}

bool loadJsonFromProcess(VarRef root, const char* const * args, const char * const *env)
{
    return loadJsonFromProcess(root, args, env, subprocess_option_enable_async | subprocess_option_no_window);
}

bool loadJsonFromProcess(VarRef root, const char* const * args, const char * const *env, int options)
{
    subprocess_s proc;
    if(!createProcess(&proc, args, env, options))
        return false;

    bool ok = loadJsonFromProcess(root, &proc, args[0]);
    subprocess_destroy(&proc);
    return ok;
}

bool loadJsonFromProcess(VarRef root, subprocess_s* proc, const char* procname)
{
    char buf[12*1024];
    ProcessReadStream ps(proc, ProcessReadStream::DONTTOUCH, &buf[0], sizeof(buf));

    bool ok = serialize::load(root, ps, serialize::AUTO);

    if(ok)
    {
        logdebug("[%s] ingested, waiting until it exits...", procname);
    }
    else
    {
        procfail(ps, procname);
        if(subprocess_alive(proc))
        {
            logerror("[%s] failed to parse and still alive, killing", procname);
            subprocess_terminate(proc);
        }
    }

    int ret = 0;
    int err = subprocess_join(proc, &ret);
    if(err)
    {
        logerror("[%s] subprocess_join failed", procname);
        return false;
    }
    ok = !ret; // if the process reports failure, don't use it even if it's valid json
    logdebug("[%s] exited with code %d", procname, ret);
    if(subprocess_stderr(proc))
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
                logerror("---- [%s] begin stderr dump ----", procname);
                hdr = true;
            }
            fwrite(buf, 1, rd, stdout);
        }
        if(hdr)
            logerror("---- [%s] end stderr dump ----", procname);
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
    bool ok = loadJsonFromProcess(tree->root(), args, NULL);
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
