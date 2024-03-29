#include "jsonstreamwrapper.h"
#include <stdio.h>
#include <assert.h>
#include <utility>
#include <string.h>
#include <algorithm>

BufferedReadStream::BufferedReadStream(InitFunc initf, ReadFunc rf, char* buf, size_t bufsz)
    : _cur(0), _buf(buf), _end(0), _dst(0), _bufsz(bufsz), _lastread(0), _count(0)
    , _readf(rf), _initf(initf), _eof(false), _autoEOF(true)
{
    assert(rf && buf && bufsz);
}

BufferedReadStream::~BufferedReadStream()
{
}

void BufferedReadStream::init()
{
    if (!_cur)
    {
        _Refill();
        if(_initf)
            _initf(this);
    }
}

const BufferedReadStream::Ch* BufferedReadStream::Peek4() const
{
    return availBuffered() >= 4 ? _cur : 0;
}

void BufferedReadStream::setEOF()
{
    if(!_eof)
    {
        _buf[0] = '\0';
        _buf[1] = '\0';
        _eof = true;
        _end = _buf;
    }
}

void BufferedReadStream::advanceBuffered(size_t n)
{
    assert(_cur + n <= _end);
    _cur += n;
    if(_cur == _end)
        _Refill();
}

void BufferedReadStream::_Refill()
{
    if (!_eof)
    {
        _count += _lastread;
        const size_t rd = _readf(_buf, _bufsz, this);
        _lastread = rd;
        _end = _buf + rd;
        _cur = _buf;

        if(!rd && _autoEOF)
            setEOF();
    }
}

BufferedWriteStream::BufferedWriteStream(InitFunc initf, WriteFunc wf, char* buf, size_t bufsz)
    : _dst(NULL), _buf(buf), _last(buf + bufsz), _count(0), _writef(wf), _err(false), _initf(initf)
{
}

BufferedWriteStream::~BufferedWriteStream()
{
    if(_dst)
        Flush();
}

void BufferedWriteStream::Flush()
{
    assert(_dst);
    if (!_err && _dst != _buf)
    {
        const size_t avail = _dst - _buf;
        const size_t written = _writef(_buf, avail, this);
        _count += written;
        _err = written != avail;
    }
    _dst = _buf;
}

void BufferedWriteStream::init()
{
    if(!_dst)
    {
        _dst = _buf;
        if(_initf)
            _initf(this);
    }
}

void BufferedWriteStream::Write(const char* buf, size_t n)
{
    while(n)
    {
        const size_t space = _last - _dst;
        const size_t copy = std::min(space, n);
        memcpy(_dst, buf, copy);
        _dst += copy;
        if(_dst == _last)
            Flush();
        n -= copy;
        buf += copy;
    }
}

void BufferedWriteStream::WriteStr(const char* s)
{
    while(*s)
        this->Put(*s++);
}

BufferedWriteStream::BufInfo BufferedWriteStream::getBuffer() const
{
    assert(_dst && _dst <= _last);
    BufInfo b{ _dst, static_cast<size_t>(_last - _dst) };
    return b;
}

void BufferedWriteStream::advanceBuffer(size_t n)
{
    assert(_dst + n  <= _last);
    _dst += n;
    if(_dst == _last)
        Flush();
}


BufferedFILEReadStream::BufferedFILEReadStream(void *FILEp, char* buf, size_t bufsz)
    : BufferedReadStream(NULL, _Read, buf, bufsz), _fh(FILEp)
{
}

size_t BufferedFILEReadStream::_Read(void* dst, size_t bytes, BufferedReadStream* self)
{
    BufferedFILEReadStream *me = static_cast<BufferedFILEReadStream*>(self);
    FILE *fh = static_cast<FILE*>(me->_fh);
    return fread(dst, 1, bytes, fh);
}

InplaceStringStream::InplaceStringStream(char* s, size_t len)
    : BufferedReadStream(NULL, _Read, s, len)
{
}

size_t InplaceStringStream::_Read(void* dst, size_t bytes, BufferedReadStream* self)
{
    // we have the entire string right from the start.
    // first time must succeed.
    // 2nd time around there is nothing left.
    return self->_cur ? 0 : self->_bufsz;
}

BufferedFILEWriteStream::BufferedFILEWriteStream(void* FILEp, char* buf, size_t bufsz)
    : BufferedWriteStream(NULL, _Write, buf, bufsz), _fh(FILEp)
{
}

size_t BufferedFILEWriteStream::_Write(const void* src, size_t bytes, BufferedWriteStream* self)
{
    BufferedFILEWriteStream* me = static_cast<BufferedFILEWriteStream*>(self);
    FILE* fh = static_cast<FILE*>(me->_fh);
    return fwrite(src, 1, bytes, fh);
}
