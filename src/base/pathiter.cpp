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
    assert(*s == '/');

    _cur.s = s+1;
    _cur.len = Adv(s+1) - (s+1);
}

PathIter::~PathIter()
{
}

PathIter& PathIter::operator++()
{
    assert(hasNext());
    const char *next = _cur.s + _cur.len + 1;
    const char *end = Adv(next);
    _cur.len = end - next;
    _cur.s = next;

    return *this;
}
