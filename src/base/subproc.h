#pragma once

#include "jsonstreamwrapper.h"

#include <future>
#include <vector>

struct subprocess_s;
class DataTree;

// args[0] is the executable, args[1...] the params. args[] is terminated by a NULL entry.
// output from process is parsed into json tree
bool loadJsonFromProcess(DataTree *tree, const char **args, const char **env, int options);
bool loadJsonFromProcess(DataTree *tree, subprocess_s *proc, const char *procname);

bool createProcess(subprocess_s *proc, const char** args, const char** env, int options);

struct AsyncLaunchConfig
{
    std::vector<std::string> args;
};

DataTree *loadJsonFromProcessSync(AsyncLaunchConfig&& cfg);

// start process in bg thread and parse as json whatever it spews to stdout.
// delete the tree ptr when it's no longer needed
std::future<DataTree*> loadJsonFromProcessAsync(AsyncLaunchConfig&& cfg);


// Adapted from rapidjson/filereadstream.h
class ProcessReadStream : public BufferedReadStream
{
public:
    enum CloseBehavior
    {
        DONTTOUCH,
        WAIT,
        TERMINATE
    };

    ProcessReadStream(subprocess_s *proc, CloseBehavior close, char* buf, size_t bufsz);
    ~ProcessReadStream();

    int wait();
    void terminate();

protected:
    ProcessReadStream(InitFunc initf, ReadFunc readf, subprocess_s* proc, CloseBehavior close, char* buf, size_t bufsz);
    static size_t _Read(void* dst, size_t bytes, BufferedReadStream* self);

private:
    subprocess_s *_proc;
    CloseBehavior closeb;
};