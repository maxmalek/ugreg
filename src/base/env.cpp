#ifdef _WIN32
#  define STRICT
#  define NOGDI
#  define NOMINMAX
#  define NOSERVICE
#  define NOMCX
#  define NOIME 
#  include <Windows.h> // since the below doesn't work, just include the kitchen sink... sigh
//#  include <processenv.h> // GetEnvironmentStrings()
//#  include <stringapiset.h> // WideCharToMultiByte()
#else
#  include "unistd.h"
  extern char **environ; // POSIX, environ(7)
#endif

#include "env.h"
#include <assert.h>


std::vector<std::string> enumerateEnvVars()
{
    std::vector<std::string> ret;

#ifdef _WIN32
    LPWCH env = ::GetEnvironmentStringsW();
    std::vector<char> tmp(1024);
    for(LPCWCH e = env; *e;) // env ends with \0\0 ...
    {
        // Windows requires UTF-8 conversion... sigh^2
        // get target buffer size
        int wc = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            e, -1, NULL, 0, 0, NULL);
        assert(wc > 0);
        tmp.resize(wc);

        // actually convert the thing
        wc = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            e, -1, &tmp[0], tmp.size(), 0, NULL);

        assert(wc > 0 && size_t(wc) == tmp.size());

        ret.push_back(&tmp[0]);
        e += ret.back().length() + 1; // skip over \0
    }
    ::FreeEnvironmentStringsW(env);
#else
    for (const char** env = environ; *env != 0; env++)
        ret.push_back(*env);
#endif

    return ret;
}
