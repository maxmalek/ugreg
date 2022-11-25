#include "util.h"

struct CasefoldData
{
    const unsigned short * const keys; // lower 16 bits of key
    const unsigned short * const values; // lower 16 bits of value
    const unsigned short * const index;
    unsigned expansion; // how many chars this casefold expands into
    unsigned high; // for anything that doesn't fit into 16 bits
};

#include "casefold.gen.h"
#include "utf8casefold.h"
#include <assert.h>

static const char BADCHAR = '?';


static unsigned casefold_1(unsigned x, const CasefoldData *dat)
{
    const unsigned h = casefold_tabindex(x);
    const unsigned begin = dat->index[h];
    const unsigned end   = dat->index[h+1];
    const unsigned short * const k = dat->keys;
    for(unsigned i = begin; i < end; ++i)
        if(x == k[i])
            return dat->values[i];
    return 0;
}

// Simple casefolding: Exchange 1 char; does not increase the size of the string
// when encoded back into utf-8
unsigned utf8casefold1(unsigned x)
{
    // The ASCII range is simple
    if(x >= 'A' && x <= 'Z')
        return x + ('a' - 'A');

    if(x < 128) // Not a foldable char
        return x;

    for(size_t i = 0; i < Countof(casefoldData); ++i)
    {
        const CasefoldData * const dat = &casefoldData[i];
        if(dat->expansion > 1)
            continue;
        unsigned c = x - dat->high; // This can underflow
        if(c <= 0xffff) // Check fails also when underflowed
        {
            c = casefold_1(c, dat);
            if(c)
                return c + dat->high;
        }
    }
    return x; // Not foldable
}

// valid codepoint if >= 0; < 0 on error
int utf8read(const char *& s, size_t& len)
{
    unsigned char a = *s++;
    if(a < 128) // 1 byte, ASCII
    {
        --len;
        return (int)(unsigned)a;
    }

    unsigned n, ret;

    if((a & 0xe0) == 0xc0) // 2 bytes
    {
        n = 2;
        ret = a & 0x1f;
    }
    else if((a & 0xf0) == 0xe0) // 3 chars
    {
        n = 3;
        ret = a & 0xf;
    }
    else if((a & 0xf8) == 0xf0) // 4 chars
    {
        n = 4;
        ret = a & 0x7;
    }
    else
        return -1; // wrong encoding or s was the middle of a codepoint

    if(len < n)
        return -1; // truncated

    len -= n;

    do
    {
        unsigned char x = *s++;
        if((x & 0xc0) != 0x80)
            return -1;
        ret <<= 6;
        ret |= (x & 0x3f);
    }
    while(--n);

    return (int)ret;
}

int utf8write(char *s, unsigned c)
{
    if(c < 0x80) // encodes to 1 byte
    {
        *s++ = c;
        return 1;
    }


    if(c < 0x800) // encodes to 2 bytes
    {
        *s++ = char(0xc0 | (c >> 6));   // 110xxxxx
        *s++ = char(0x80 | (c & 0x3f)); // 10xxxxxx
        return 2;
    }

    // these are invalid
    if(c == 0xffff || c == 0xfffe)
    {
        *s++ = BADCHAR;
        return 0;
    }

    if(c < 0x10000) // encodes to 3 bytes
    {
        switch (c)
        {
            case 0xD800:
            case 0xDB7F:
            case 0xDB80:
            case 0xDBFF:
            case 0xDC00:
            case 0xDF80:
            case 0xDFFF:
               *s++ = BADCHAR;
               return 0;
        }
        *s++ = char(0xe0 | (c >> 12));
        *s++ = char(0x80 | ((c >> 6) & 0x3f));
        *s++ = char(0x80 | ((c     ) & 0x3f));
        return 3;
    }

    if(c > 0x10FFFF) // out of range
        *s++ = BADCHAR;

    // encodes to 4 bytes
    *s++ = char(0xf0 | (c >> 18));
    *s++ = char(0x80 | ((c >> 12) & 0x3f));
    *s++ = char(0x80 | ((c >> 6) & 0x3f));
    *s++ = char(0x80 | ((c     ) & 0x3f));
    return 4;
}

int utf8casefoldcopy(std::vector<unsigned char>& vec, const char* s, size_t len)
{
    unsigned char enc[4];
    int ret = 0;
    while(len)
    {
        int c = utf8read(s, len); // modifies s, len
        if(c < 0)
            return -1;
        c = utf8casefold1(c);
        int n = utf8write((char*)&enc[0], c);
        if(n <= 0)
            return -1;
        assert(n <= 4);
        vec.insert(vec.end(), &enc[0], &enc[n]);
        ++ret;
    }
    return ret;
}
