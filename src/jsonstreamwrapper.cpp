#include "jsonstreamwrapper.h"
#include <stdio.h>
#include <assert.h>

BufferedReadStream::BufferedReadStream(ReadFunc rf, char* buf, size_t bufsz)
    : _cur(0), _buf(buf), _last(0), _dst(0), _bufsz(bufsz), _lastread(0), _count(0)
    , _readf(rf), _eof(false)
{
    assert(rf && buf && bufsz > 4);
}

BufferedReadStream::~BufferedReadStream()
{
}

void BufferedReadStream::init()
{
    if (!_cur)
        _Refill();
}

const BufferedReadStream::Ch* BufferedReadStream::Peek4() const
{
    return (_cur + 4 - !_eof <= _last) ? _cur : 0;
}

void BufferedReadStream::_Refill()
{
    if (!_eof)
    {
        _count += _lastread;
        const size_t rd = _readf(_buf, _bufsz, this);
        _lastread = rd;
        _last = _buf + rd - 1;
        _cur = _buf;

        if(!rd)
        {
            _buf[rd] = '\0';
            _eof = true;
            ++_last;
        }
    }
}

BufferedWriteStream::BufferedWriteStream(WriteFunc wf, char* buf, size_t bufsz)
    : _dst(buf), _buf(buf), _last(buf + bufsz), _count(0), _writef(wf), _err(false)
{
}

BufferedWriteStream::~BufferedWriteStream()
{
    Flush();
}

void BufferedWriteStream::Flush()
{
    if (!_err && _dst != _buf)
    {
        const size_t avail = _dst - _buf;
        const size_t written = _writef(_buf, avail, this);
        _count += written;
        _err = written != avail;
    }
    _dst = _buf;
}


BufferedFILEReadStream::BufferedFILEReadStream(void *FILEp, char* buf, size_t bufsz)
    : BufferedReadStream(_Read, buf, bufsz), _fh(FILEp)
{
}

size_t BufferedFILEReadStream::_Read(void* dst, size_t bytes, BufferedReadStream* self)
{
    BufferedFILEReadStream *me = static_cast<BufferedFILEReadStream*>(self);
    FILE *fh = static_cast<FILE*>(me->_fh);
    return fread(dst, 1, bytes, fh);
}

InplaceStringStream::InplaceStringStream(char* s, size_t len)
    : BufferedReadStream(_Read, s, len)
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
    : BufferedWriteStream(_Write, buf, bufsz), _fh(FILEp)
{
}

size_t BufferedFILEWriteStream::_Write(const void* dst, size_t bytes, BufferedWriteStream* self)
{
    BufferedFILEWriteStream* me = static_cast<BufferedFILEWriteStream*>(self);
    FILE* fh = static_cast<FILE*>(me->_fh);
    return fwrite(dst, 1, bytes, fh);
}
