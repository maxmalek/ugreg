#include <string.h>
#include "variant.h"
#include "bj.h"
#include "jsonstreamwrapper.h"
#include "json_in.h"
#include "osfunc.h"

bool loadbj(FILE *fn)
{
    char buf[8*1024];
    DataTree tre;
    BufferedFILEReadStream rd(fn, buf, sizeof(buf));
    rd.init();
    return bj::decode_json(tre.root(), rd);
}


int main(int argc, char **argv)
{
    if(argc <= 1) // fuzzing mode -- no args
        return !loadbj(stdin);


    // verify mode -- passed subdirs with test cases
    for(int a = 1; a < argc; ++a)
    {
        const char *dir = argv[a];
        const size_t dirlen = strlen(dir);
        printf("Directory: %s\n", dir);
        DirListW list;
        if(!dirlist(list, dir))
            continue;

        DirListEntryW::StringType fn;
        for(size_t i = 0; i < list.size(); ++i)
        {
            if(!list[i].isdir)
            {
                fn = DirListEntryW::StringType(dir, dir + dirlen); // ugly hack for char* -> wstring
                fn += '/';
                fn += list[i].fn;
                std::string pfn(fn.begin(), fn.end()); // ugly hack for wstring -> something that printf() is ok with
                printf("## %s ...\n", pfn.c_str());
#ifdef _WIN32
                FILE *fh = _wfopen(fn.c_str(), L"rb");
#else
                FILE *fh = fopen(fn.c_str(), "rb");
#endif
                if(!fh)
                {
                    printf("Failed to open: %s\n", pfn.c_str());
                    continue;
                }
                loadbj(fh);
                fclose(fh);
                puts("OK");
            }
        }
    }
}
