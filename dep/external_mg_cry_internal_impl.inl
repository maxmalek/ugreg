#include <stdio.h>

static void mg_cry_internal_impl(const struct mg_connection* conn,
    const char* func,
    unsigned line,
    const char* fmt,
    va_list ap)
{
    printf("### %s:%u: ", func, line);
    vprintf(fmt, ap);
    putchar('\n');
}
