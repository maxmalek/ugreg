#pragma once

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
    typedef bool (*IsEofFunc)(BufferedReadStream* self);

    // The passed in buffer must stay alive while the stream is in use!
    // Pass eoff == NULL for the default EOF behavior: EOF is when less bytes
    // than requested could be read. Pass a custom function to override that behavior.
    BufferedReadStream(ReadFunc rf, IsEofFunc eoff, char *buf, size_t bufsz);
    ~BufferedReadStream();

    inline Ch Peek() const { return *_cur; } // If this crashes: Did you forget to call init()?
    inline Ch Take()
    {
        Ch c = *_cur; // If this crashes: Did you forget to call init()?
        if(_cur < _last)
            ++_cur;
        else
            Refill();
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
        return _eof && _cur == _last;
    }


    // ----- DO NOT TOUCH ------------

    void Refill();

    Ch* _cur;
    Ch* const _buf;
    Ch* _last;
    Ch* _dst;
    const size_t _bufsz;
    size_t _lastread;
    size_t _count;  //!< Number of characters read
    const ReadFunc _readf;
    const IsEofFunc _eoff;
    bool _eof;
};

class BufferedFILEStream : public BufferedReadStream
{
public:
    BufferedFILEStream(void *FILEp, char *buf, size_t bufsz);
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
