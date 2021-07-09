#pragma once

#include "jsonstreamwrapper.h"

struct subprocess_s;
class DataTree;

// args[0] is the executable, args[1...] the params. args[] is terminated by a NULL entry.
DataTree *loadJsonFromProcess(const char **args);

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

private:

    static size_t _Read(void* dst, size_t bytes, BufferedReadStream* self);
    static bool _IsEOF(BufferedReadStream* self);

    subprocess_s *_proc;
    CloseBehavior closeb;
};
