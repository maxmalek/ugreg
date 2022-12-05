#pragma once

#include <stddef.h>
#include <stdarg.h>

enum LogLevel
{
    LL_DEV,
    LL_DEBUG,
    LL_NORMAL,
    LL_ERROR,

    LL_MAX // not used; for array sizing
};

#ifdef _MSC_VER
#  define ATTR_PRINTF(a)
#else // gcc, clang
#  define ATTR_PRINTF(a) __attribute__ ((format (printf, a, (a)+1)));
#endif

typedef void (*log_callback_func)(unsigned level, const char *message, size_t len, void *);

void log_setConsoleLogLevel(LogLevel level);
void vlogx(LogLevel, int nl, const char *fmt, va_list va);
void logx(LogLevel, int nl, const char *fmt, ...) ATTR_PRINTF(3);
void log(const char *fmt, ...) ATTR_PRINTF(1);
void logerror(const char *fmt, ...) ATTR_PRINTF(1);
void logdebug(const char *fmt, ...) ATTR_PRINTF(1);
void logdev(const char *fmt, ...) ATTR_PRINTF(1);

#if defined(_DEBUG) || !defined(NDEBUG)
#define DEBUG_LOG(...) do { logdev(__VA_ARGS__); } while(0)
#else
#define DEBUG_LOG(...) do {} while(0)
#endif
