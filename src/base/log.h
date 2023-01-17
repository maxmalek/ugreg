#pragma once

#include <stddef.h>
#include <stdarg.h>

enum LogLevel
{
    LL_ERROR,  // warnings and errors
    LL_NORMAL, // generic; low-volume
    LL_DEBUG,  // help sysadmins to find issues
    LL_DEV,    // logspam; only relevant for devs

    LL_MAX // not used; for array sizing
};

#ifdef _MSC_VER
#  define ATTR_PRINTF(a)
#else // gcc, clang
#  define ATTR_PRINTF(a) __attribute__ ((format (printf, a, (a)+1)));
#endif

bool log_openfile(const char *fn);
void log_closefile();

void log_setConsoleLogLevel(LogLevel level);
LogLevel log_getConsoleLogLevel();
void vlogx(LogLevel, int nl, const char *fmt, va_list va);
void logx(LogLevel, int nl, const char *fmt, ...) ATTR_PRINTF(3);
void log(const char *fmt, ...) ATTR_PRINTF(1);
void logerror(const char *fmt, ...) ATTR_PRINTF(1);
void logdebug(const char *fmt, ...) ATTR_PRINTF(1);
void logdev(const char *fmt, ...) ATTR_PRINTF(1);

#if defined(_DEBUG) || !defined(NDEBUG)
#define DEBUG_LOG(...) do { logdev(__VA_ARGS__); } while(0)
#define DEBUG_LOGX(...) do { logx(__VA_ARGS__); } while(0)
#else
#define DEBUG_LOG(...) do {} while(0)
#define DEBUG_LOGX(...) do {} while(0)
#endif
