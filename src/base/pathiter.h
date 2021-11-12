#include "types.h"

// Iterate over path fragments (str, len)
// So that "/a/bb/ccc" yields: ("a", 1) ("bb", 2) ("ccc", 3)
// Beware: Parts are not \0-terminated!
// FIXME: correctly decode JSON pointers here (escapes and #)
// see https://rapidjson.org/md_doc_pointer.html#JsonPointer
// and https://datatracker.ietf.org/doc/html/rfc6901
class PathIter
{
public:
    PathIter(const char *s);
    ~PathIter();

    PoolStr value() const { return _cur; }
    const char* remain() const { return _cur.s + _cur.len; }
    bool hasNext() const { return !!*_cur.s; }

    PathIter& operator++();
    PathIter operator++(int) { PathIter tmp = *this; ++(*this); return tmp; }
    friend bool operator== (const PathIter& a, const PathIter& b)
    {
        return a._cur.s == b._cur.s;
    }
    friend bool operator!= (const PathIter& a, const PathIter& b)
    {
        return a._cur.s != b._cur.s;
    }

private:
    PoolStr _cur;
};

