#include <stdlib.h>
#include <atomic>
#include "datatree.h"
#include "serverutil.h"


std::atomic<bool> s_quit;

static void sigquit(int)
{
    s_quit = true;
    handlesigs(NULL);
}

int main(int argc, char** argv)
{
    handlesigs(sigquit);

    DataTree cfgtree;
    if (!doargs(cfgtree, argc, argv))
        bail("Failed to handle cmdline. Exiting.", "");

    return 0;
}
