#include "osfunc.h"

#include <assert.h>
#include "util.h"

#ifdef _WIN32
#define STRICT
#define NOGDI
#define NOMINMAX
#define NOSERVICE
#define NOMCX
#define NOIME
#include <Windows.h>
#else // posix
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/dir.h>
#endif

template<typename T>
static inline bool dirlistSkip(const T *fn)
{
    return *fn == '.' && (!fn[1] || (fn[1] == '.' && !fn[2]));
}

#ifdef _WIN32

HANDLE win32_FindFirstFile(const char *path, WIN32_FIND_DATAW *pfd)
{
    int len = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if(len <= 0)
        return false;

    WCHAR wpath[MAX_PATH + 32];
    if(len > Countof(wpath) - 4)
        return false;

    len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wpath, len);
    if(len <= 0)
        return false;

    // len includes the terminating zero byte, which is one too long
    --len;

    if(!(wpath[len] == '/' || wpath[len] == '\\'))
        wpath[len++] = '\\';
    wpath[len++] = '*';
    wpath[len++] = 0;

    return ::FindFirstFileW(wpath, pfd);
}

bool dirlist(DirListW & list, const char * path)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = win32_FindFirstFile(path, &fd);

    if (h == INVALID_HANDLE_VALUE)
        return false;

    do
    {
        if (!dirlistSkip(fd.cFileName))
        {
            DirListEntryW e { fd.cFileName, !!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) };
            list.push_back(std::move(e));
        }
    }
    while (::FindNextFileW(h, &fd));
    ::FindClose(h);
    return true;
}
#endif

bool dirlist(DirList & list, const char * path)
{
    assert(*path);
    if(!*path)
        return false;

#ifdef _WIN32
    WIN32_FIND_DATAW fd;
    HANDLE h = win32_FindFirstFile(path, &fd);

    if (h == INVALID_HANDLE_VALUE)
        return false;

    char fbuf[4 * MAX_PATH + 1]; // UTF-8 is max. 4 bytes per char, and cFileName is an array of MAX_PATH elements
    do
    {
        if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, fd.cFileName, -1, fbuf, sizeof(fbuf), 0, NULL))
        {
            if (!dirlistSkip(fbuf))
            {
                DirListEntry e { fbuf, !!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) };
                list.push_back(std::move(e));
            }
        }
    }
    while (::FindNextFileW(h, &fd));
    ::FindClose(h);

#else // posix

    struct dirent * dp;
    int pathfd = ::open(path, O_DIRECTORY, 0); // Refuses to open("")
    if(pathfd == -1)
        return false;
    DIR *dirp = ::fdopendir(pathfd);
    if(!dirp)
    {
        ::close(pathfd);
        return false;
    }

    while((dp=::readdir(dirp)) != NULL)
        if(!dirlistSkip(dp->d_name))
        {
            DirListEntry e { dp->d_name, dp->d_type == DT_DIR };
            list.push_back(std::move(e));
        }

    ::closedir(dirp); // also closes pathfd

#endif

    return true;
}
