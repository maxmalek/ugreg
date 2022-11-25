#include "strmatch.h"

// Adapted via on http://git.musl-libc.org/cgit/musl/plain/src/string/memmem.c

#include <string.h>
#include <stdint.h>
#include "utf8casefold.h"

static char* twobyte_memmem(const unsigned char* h, size_t k, const unsigned char* n)
{
    uint16_t nw = n[0] << 8 | n[1], hw = h[0] << 8 | h[1];
    for (h += 2, k -= 2; k; k--, hw = hw << 8 | *h++)
        if (hw == nw) return (char*)h - 2;
    return hw == nw ? (char*)h - 2 : 0;
}

static char* threebyte_memmem(const unsigned char* h, size_t k, const unsigned char* n)
{
    uint32_t nw = (uint32_t)n[0] << 24 | n[1] << 16 | n[2] << 8;
    uint32_t hw = (uint32_t)h[0] << 24 | h[1] << 16 | h[2] << 8;
    for (h += 3, k -= 3; k; k--, hw = (hw | *h++) << 8)
        if (hw == nw) return (char*)h - 3;
    return hw == nw ? (char*)h - 3 : 0;
}

static char* fourbyte_memmem(const unsigned char* h, size_t k, const unsigned char* n)
{
    uint32_t nw = (uint32_t)n[0] << 24 | n[1] << 16 | n[2] << 8 | n[3];
    uint32_t hw = (uint32_t)h[0] << 24 | h[1] << 16 | h[2] << 8 | h[3];
    for (h += 4, k -= 4; k; k--, hw = hw << 8 | *h++)
        if (hw == nw) return (char*)h - 4;
    return hw == nw ? (char*)h - 4 : 0;
}

#define BITOP(a,b,op) \
 ((a)[(size_t)(b)/(8*sizeof *(a))] op (size_t)1<<((size_t)(b)%(8*sizeof *(a))))



TwoWayMatcher::TwoWayMatcher(const char* needle, size_t len)
{
    const unsigned char *n = (const unsigned char*)needle;
    _needle.assign(needle, needle + len);
    init();
}

TwoWayMatcher::TwoWayMatcher()
{
}

void TwoWayMatcher::init()
{
    size_t l = _needle.size();
    if(l <= 4)
        return; // leaves a bunch of stuff uninitialized, this is fine

    const unsigned char *n = _needle.data();

    memset(byteset, 0, sizeof(byteset));

    /* Computing length of needle and fill shift table */
    for (size_t i = 0; i < l; i++)
        BITOP(byteset, n[i], |=), shift[n[i]] = i + 1;

    /* Compute maximal suffix */
    size_t ip, jp, k, p;
    ip = -1; jp = 0; k = p = 1;
    while (jp + k < l) {
        if (n[ip + k] == n[jp + k]) {
            if (k == p) {
                jp += p;
                k = 1;
            }
            else k++;
        }
        else if (n[ip + k] > n[jp + k]) {
            jp += k;
            k = 1;
            p = jp - ip;
        }
        else {
            ip = jp++;
            k = p = 1;
        }
    }
    size_t ms = ip;
    size_t p0 = p;

    /* And with the opposite comparison */
    ip = -1; jp = 0; k = p = 1;
    while (jp + k < l) {
        if (n[ip + k] == n[jp + k]) {
            if (k == p) {
                jp += p;
                k = 1;
            }
            else k++;
        }
        else if (n[ip + k] < n[jp + k]) {
            jp += k;
            k = 1;
            p = jp - ip;
        }
        else {
            ip = jp++;
            k = p = 1;
        }
    }
    if (ip + 1 > ms + 1) ms = ip;
    else p = p0;

    /* Periodic needle? */
    if (memcmp(n, n + p, ms + 1)) {
        mem0 = 0;
        p = std::max(ms, l - ms - 1) + 1;
    }
    else mem0 = l - p;

    this->p = p;
    this->ms = ms;
}

const unsigned char* TwoWayMatcher::twoway_match(const unsigned char* h, const unsigned char* z) const
{
    const unsigned char * const n = _needle.data();
    const size_t l = _needle.size();

    const size_t p = this->p;
    const size_t ms = this->ms;

    size_t mem = 0;
    size_t k;

    /* Search loop */
    for (;;) {
        /* If remainder of haystack is shorter than needle, done */
        if (z - h < ptrdiff_t(l)) return NULL;

        /* Check last byte first; advance by shift on mismatch */
        if (BITOP(byteset, h[l - 1], &)) {
            k = l - shift[h[l - 1]];
            if (k) {
                if (k < mem) k = mem;
                h += k;
                mem = 0;
                continue;
            }
        }
        else {
            h += l;
            mem = 0;
            continue;
        }

        /* Compare right half */
        for (k = std::max(ms + 1, mem); k < l && n[k] == h[k]; k++);
        if (k < l) {
            h += k - ms;
            mem = 0;
            continue;
        }
        /* Compare left half */
        for (k = ms + 1; k > mem && n[k - 1] == h[k - 1]; k--);
        if (k <= mem) return h;
        h += p;
        mem = mem0;
    }
}

const char* TwoWayMatcher::match(const char* haystack, size_t len) const
{
    const size_t l = _needle.size();
    size_t k = len;

    /* Return immediately on empty needle */
    if (!l) return haystack;

    /* Return immediately when needle is longer than haystack */
    if (k < l) return NULL;

    const unsigned char* h = (const unsigned char*)haystack;
    const unsigned char * const n = _needle.data();

    /* Use faster algorithms for short needles */
    h = (const unsigned char*)memchr(h, *n, k);
    if (!h || l == 1) return (const char*)h;
    k -= h - (const unsigned char*)haystack;
    if (k < l) return NULL;
    if (l == 2) return twobyte_memmem(h, k, n);
    if (l == 3) return threebyte_memmem(h, k, n);
    if (l == 4) return fourbyte_memmem(h, k, n);

    return (const char*)twoway_match(h, h + k);
}

TwoWayCasefoldMatcher::TwoWayCasefoldMatcher(const char* needle, size_t len)
{
    utf8casefoldcopy(_needle, needle, len);
    init();
}
