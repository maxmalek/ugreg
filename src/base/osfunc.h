#pragma once

#include <string>
#include <vector>


struct DirListEntry
{
    typedef std::string StringType;
    std::string fn;
    bool isdir;
};
typedef std::vector<DirListEntry> DirList;

#ifdef _WIN32
struct DirListEntryW
{
    typedef std::wstring StringType;
    std::wstring fn;
    bool isdir;
};
typedef std::vector<DirListEntryW> DirListW;
bool dirlist(DirListW& list, const char *path);
#else
typedef DirList DirListW;
typedef DirListEntry DirListEntryW;
#endif

bool dirlist(DirList& list, const char *path);


