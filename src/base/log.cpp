#include "log.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <vector>
#include <stdarg.h>
#include <stdio.h>
#include <mutex>

#include "util.h"

static std::mutex s_mtx;
#define LOCK_SCOPE() std::unique_lock _lock(s_mtx)

enum ConsoleColor
{
    NONE = -1,
    BLACK,
    RED,
    GREEN,
    BROWN,
    BLUE,
    MAGENTA,
    CYAN,
    GREY,
    YELLOW,
    LRED,
    LGREEN,
    LBLUE,
    LMAGENTA,
    LCYAN,
    WHITE,

    MAX_COLORS
};

#ifdef _DEBUG
static LogLevel s_consoleLogLevel = LL_DEV;
#else
static LogLevel s_consoleLogLevel = LL_NORMAL;
#endif

void log_setConsoleLogLevel(LogLevel level)
{
    s_consoleLogLevel = (level < LL_MAX) ? level : LL_DEV;
}

LogLevel log_getConsoleLogLevel()
{
    return s_consoleLogLevel;
}


static void _log_setcolor(bool stdout_stream, ConsoleColor color)
{
#ifdef _WIN32

    static WORD WinColorFG[MAX_COLORS] =
    {
        0,                                                  // BLACK
        FOREGROUND_RED,                                     // RED
        FOREGROUND_GREEN,                                   // GREEN
        FOREGROUND_RED | FOREGROUND_GREEN,                  // BROWN
        FOREGROUND_BLUE,                                    // BLUE
        FOREGROUND_RED |                    FOREGROUND_BLUE,// MAGENTA
        FOREGROUND_GREEN | FOREGROUND_BLUE,                 // CYAN
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,// WHITE
        // YELLOW
        FOREGROUND_RED | FOREGROUND_GREEN |                   FOREGROUND_INTENSITY,
        // RED_BOLD
        FOREGROUND_RED |                                      FOREGROUND_INTENSITY,
        // GREEN_BOLD
        FOREGROUND_GREEN |                   FOREGROUND_INTENSITY,
        FOREGROUND_BLUE | FOREGROUND_INTENSITY,             // BLUE_BOLD
        // MAGENTA_BOLD
        FOREGROUND_RED |                    FOREGROUND_BLUE | FOREGROUND_INTENSITY,
        // CYAN_BOLD
        FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
        // WHITE_BOLD
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
    };
    static_assert(Countof(WinColorFG) == MAX_COLORS);

    HANDLE hConsole = GetStdHandle(stdout_stream ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE );
    SetConsoleTextAttribute(hConsole, WinColorFG[color]);
#else

    enum ANSITextAttr
    {
        TA_NORMAL=0,
        TA_BOLD=1,
        TA_BLINK=5,
        TA_REVERSE=7
    };

    enum ANSIFgTextAttr
    {
        FG_BLACK=30, FG_RED,  FG_GREEN, FG_BROWN, FG_BLUE,
        FG_MAGENTA,  FG_CYAN, FG_WHITE, FG_YELLOW
    };

    enum ANSIBgTextAttr
    {
        BG_BLACK=40, BG_RED,  BG_GREEN, BG_BROWN, BG_BLUE,
        BG_MAGENTA,  BG_CYAN, BG_WHITE
    };

    static int UnixColorFG[MAX_COLORS] =
    {
        FG_BLACK,                                           // BLACK
            FG_RED,                                             // RED
            FG_GREEN,                                           // GREEN
            FG_BROWN,                                           // BROWN
            FG_BLUE,                                            // BLUE
            FG_MAGENTA,                                         // MAGENTA
            FG_CYAN,                                            // CYAN
            FG_WHITE,                                           // WHITE
            FG_YELLOW,                                          // YELLOW
            FG_RED,                                             // LRED
            FG_GREEN,                                           // LGREEN
            FG_BLUE,                                            // LBLUE
            FG_MAGENTA,                                         // LMAGENTA
            FG_CYAN,                                            // LCYAN
            FG_WHITE                                            // LWHITE
            FG_WHITE
    };
    static_assert(Countof(UnixColorFG) == MAX_COLORS);

    fprintf((stdout_stream? stdout : stderr), "\x1b[%d%sm",UnixColorFG[color],(color>=YELLOW&&color<MAX_COLORS ?";1":""));
#endif
}

static void _log_resetcolor(bool stdout_stream)
{
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(stdout_stream ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE );
    SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED );
#else
    fprintf(( stdout_stream ? stdout : stderr ), "\x1b[0m");
#endif
}

static const ConsoleColor colorLUT[] =
{
    LRED,   // ERROR
    NONE,   // NORMAL
    LCYAN,  // DEBUG
    LBLUE,  // DEV
};
static_assert(Countof(colorLUT) == LL_MAX);

static void _valogcolor(unsigned level, ConsoleColor col, int nl, const char *fmt, va_list va)
{
    if(level > (unsigned)s_consoleLogLevel)
        return;

    if(col != NONE)
        _log_setcolor(true, col);
    vprintf(fmt, va);
    if(nl)
        putchar('\n');
    if(col != NONE)
        _log_resetcolor(true);
}

static ConsoleColor getcolor(unsigned level)
{
    if(level >= LL_MAX)
        level = LL_MAX-1;
    return colorLUT[level];
}

void vlogx(LogLevel level, int nl, const char *fmt, va_list va)
{
    va_list vax;
    va_copy(vax, va);
    const ConsoleColor color = getcolor(level);

    {
        LOCK_SCOPE();
        _valogcolor(level, color, nl, fmt, va);
    }
}

void logx(LogLevel level, int nl, const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vlogx(level, nl, fmt, va);
    va_end(va);
}

void log(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vlogx(LL_NORMAL, 1, fmt, va);
    va_end(va);
}

void logerror(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vlogx(LL_ERROR, 1, fmt, va);
    va_end(va);
}


void logdebug(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vlogx(LL_DEBUG, 1, fmt, va);
    va_end(va);
}

void logdev(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vlogx(LL_DEV, 1, fmt, va);
    va_end(va);
}

