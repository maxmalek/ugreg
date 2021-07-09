#include "subproc.h"
#include "subprocess.h"
#include <assert.h>
#include "datatree.h"
#include "json_in.h"

void subproctest()
{
    const char *args[] =
    {
        "test.bat", NULL
    };
    subprocess_s proc;
    puts("------proc------");
    if(subprocess_create(args, subprocess_option_enable_async | subprocess_option_no_window, &proc))
        return; // nope

    
    char buf[4096];
    ProcessReadStream ps(&proc, ProcessReadStream::WAIT, &buf[0], sizeof(buf));

    while(!ps.done())
    {
        putchar(ps.Take());
    }

    puts("------end------");
}

DataTree* loadJsonFromProcess(const char** args, size_t readTimeoutMS, size_t totalTimeoutMS)
{
    subprocess_s proc;
    if (subprocess_create(args, subprocess_option_enable_async | subprocess_option_no_window | subprocess_option_inherit_environment, &proc))
        return NULL;

    DataTree *tree = new DataTree;
    char buf[4096];
    ProcessReadStream ps(&proc, ProcessReadStream::WAIT, &buf[0], sizeof(buf));

    assert(false); // FIXME

    return tree;
}



ProcessReadStream::ProcessReadStream(subprocess_s* proc, CloseBehavior close, char* buf, size_t bufsz)
    : BufferedReadStream(_Read, _IsEOF, buf, bufsz), _proc(proc), closeb(close)
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

bool ProcessReadStream::_IsEOF(BufferedReadStream* self)
{
    ProcessReadStream* me = static_cast<ProcessReadStream*>(self);
    return !subprocess_alive(me->_proc);
}
