#include "pathiter.h"
#include <assert.h>

static const char *Adv(const char *s)
{
    for(char c; (c = *s) && c != '/'; ++s) {}
    assert(!*s || *s == '/');
    return s;
}

PathIter::PathIter(const char* s)
{
    if(*s == '/')
        ++s;

    _cur.s = s;
    _cur.len = Adv(s) - s;
}

PathIter::~PathIter()
{
}

PathIter& PathIter::operator++()
{
    assert(hasNext());
    const char *next = _cur.s + _cur.len;
    if(*next)
    {
        assert(*next == '/');
        ++next;
        const char *end = Adv(next);
        _cur.len = end - next;
        _cur.s = next;
    }
    else
    {
        _cur.len = 0;
        _cur.s = next;
    }

    return *this;
}
