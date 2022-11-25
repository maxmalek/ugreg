#pragma once

#include <vector>


int utf8read(const char *& s, size_t& len);
int utf8write(char *s, unsigned c);
unsigned utf8casefold1(unsigned x);

// casefold s and append to vec. returns # of codepoints written, < 0 in case of error
int utf8casefoldcopy(std::vector<unsigned char>& vec, const char* s, size_t len);

// find starting utf8 codepoint so that ret <= where and first <= ret
const char *utf8FindBeginBackward(const char *where, const char *first);
