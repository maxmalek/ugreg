#pragma once

#include "types.h"

/*
This file wraps rapidjson's streams into a generic interface.
The reason is that I'd rather have one base class that implements most of the functionality
directly inlined and has a single funcptr that is rarely called.
That avoids a lot of code in headers (with the usual compile-time code duplication)
and has only minimal runtime overhead, plus we're still not using virtual calls.
*/

class BufferedReadStream
{
public:
    typedef char Ch;

    typedef size_t (*ReadFunc)(void *dst, size_t bytes, BufferedReadStream *self);
    typedef void (*InitFunc)(BufferedReadStream *self);

    // The passed in buffer must stay alive while the stream is in use!
    // Pass eoff == NULL for the default EOF behavior: EOF is when less bytes
    // than requested could be read. Pass a custom function to override that behavior.
    BufferedReadStream(InitFunc initf, ReadFunc rf, char *buf, size_t bufsz);
    ~BufferedReadStream();

    inline Ch Peek() const { return *_cur; } // If this crashes: Did you forget to call init()?
    inline Ch Take()
    {
        Ch c = *_cur; // If this crashes: Did you forget to call init()?

        if(_cur < _end)
        {
            ++_cur;
            if(_cur == _end)
                _Refill();
        }

        return c;
    }
    inline size_t Tell() const { return _count + static_cast<size_t>(_cur - _buf); }

    // For encoding detection only.
    const Ch* Peek4() const;

    // For in-situ-parsing
    inline void Put(Ch c) { *_dst++ = c; }
    inline Ch* PutBegin() { return _dst = _cur; }
    inline size_t PutEnd(Ch* begin) { return static_cast<size_t>(_dst - begin); }

    // intentionally not defined so that it can't be used as an output stream by accident
    //inline void Flush() {}


    // ------------ non-rapidjson-API -----------------

    // Call this after the ctor to fetch the initial buffer to make Peek() and Take() work
    // (We don't want to pre-load the buffer in the ctor because that may take some time that
    // is better wasted in a background thread)
    void init();

    // Check whether all consumable bytes were consumed
    inline bool done() const
    {
        return _eof && _cur == _end;
    }

    void setEOF();

    const void *ptr() const { return _cur; }
    size_t availBuffered() const { return _end - _cur; }
    void advanceBuffered(size_t n); // skip up to availBuffered() bytes

    // ----- DO NOT TOUCH ------------
    void _Refill();

    Ch* _cur;
    Ch* const _buf;
    Ch* _end;
    Ch* _dst;
    const size_t _bufsz;
    size_t _lastread;
    size_t _count;  //!< Number of characters read
    const ReadFunc _readf;
    const InitFunc _initf;
    bool _eof;
    bool _autoEOF; // stream can set this to turn off automatic eof handling when 0 bytes are read
};

class BufferedFILEReadStream : public BufferedReadStream
{
public:
    BufferedFILEReadStream(void *FILEp, char *buf, size_t bufsz);
private:
    static size_t _Read(void* dst, size_t bytes, BufferedReadStream* self);
    void * const _fh;
};

class InplaceStringStream : public BufferedReadStream
{
public:
    InplaceStringStream(char *s, size_t len); // must pass length explicitly
private:
    static size_t _Read(void* dst, size_t bytes, BufferedReadStream* self);
};


class BufferedWriteStream
{
public:
    typedef char Ch;

    typedef size_t(*WriteFunc)(const void* src, size_t bytes, BufferedWriteStream* self);
    typedef void (*InitFunc)(BufferedWriteStream* self);

    // The passed in buffer must stay alive while the stream is in use!
    // Pass eoff == NULL for the default EOF behavior: EOF is when less bytes
    // than requested could be read. Pass a custom function to override that behavior.
    BufferedWriteStream(InitFunc initf, WriteFunc wf, char* buf, size_t bufsz);
    ~BufferedWriteStream();


    inline size_t Tell() const { return _count + static_cast<size_t>(_dst - _buf); }


    inline void Put(Ch c) { *_dst++ = c; if(_dst == _last) Flush(); }

    void Flush();


    // ------------ non-rapidjson-API -----------------
    inline bool isError() const { return _err; }
    void init();

    void Write(const char *buf, size_t n);
    void WriteStr(const char *s);


    // ----- Direct memory-to-memory API --------
    struct BufInfo
    {
        Ch* buf;
        size_t remain;
    };
    BufInfo getBuffer() const; // useful when you need a buffer pointer to write to directly
    void advanceBuffer(size_t n); // use this after writing n bytes to the buffer to advance the internal state


    //------------------------------------------------------


    Ch* _dst;
    Ch* const _last;
    Ch* const _buf;

    size_t _count;  //!< Number of characters written, total
    WriteFunc _writef;
    bool _err; // if this is ever set, the disk is full or something
    const InitFunc _initf;
};

class BufferedFILEWriteStream : public BufferedWriteStream
{
public:
    BufferedFILEWriteStream(void* FILEp, char* buf, size_t bufsz);
private:
    static size_t _Write(const void* src, size_t bytes, BufferedWriteStream* self);
    void* const _fh;
};
