#pragma once

// generates a random series of characters taken from the alphabet and writes into dst.
// dst is zero-terminated, n is the total size of the buffer.
void mxGenerateToken(char *dst, size_t n, const char *alphabet, size_t alphabetSize, bool zeroterm);

// same as above but uses the default alphabet: [a-zA-Z0-9_]
void mxGenerateToken(char *dst, size_t n, bool zeroterm);
