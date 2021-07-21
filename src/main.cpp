#include <stdio.h>
#include <sstream>
#include <atomic>
#include <assert.h>
#include <signal.h>

#include "webserver.h"
#include "argh.h"

#include "config.h"
#include "handler_get.h"
#include "jsonstreamwrapper.h"
#include "json_in.h"

#include <Windows.h> // FIXME: kill this shit

#ifndef SIGQUIT
#define SIGQUIT 3
#endif

std::atomic<bool> s_quit;

static void handlesigs(void (*f)(int))
{
    if(!f)
        f = SIG_DFL;
    signal(SIGBREAK, f);
    signal(SIGQUIT, f);
    signal(SIGTERM, f);
    signal(SIGINT, f);
}

static void sigquit(int)
{
    s_quit = true;
    handlesigs(NULL);
}

static void bail(const char *a, const char *b)
{
    fprintf(stderr, "FATAL: %s%s\n", a, b);
    _exit(1);
}

static int server(const ServerConfig& cfg)
{
    WebServer::StaticInit();

    WebServer srv;
    if (!srv.start(cfg))
        bail("Failed to start server!", "");

    // FIXME: this is ugly
    TreeHandler htree(4); // strlen("/get")
    srv.registerHandler("/get", htree.Handler, &htree);

    while(!s_quit)
        Sleep(100);

    puts("Stopping server...");
    srv.stop();

    WebServer::StaticShutdown();
    return 0;
}

static void loadcfg(ServerConfig& cfg, const char *fn)
{
    FILE* f = fopen(fn, "rb");
    if(!f)
        bail("Failed to open config file: ", fn);

    DataTree tree;
    char buf[4096];
    BufferedFILEReadStream fs(f, buf, sizeof(buf));
    bool ok = loadJsonDestructive(tree.root(), fs);
    fclose(f);

    if(!ok)
        bail("Error loading config file (bad json?): ", fn);

    if(!tree.root() || tree.root().type() != Var::TYPE_MAP)
        bail("Config file did not result in useful data, something is wrong: ", fn);

    cfg.add(tree.root());
}

ServerConfig doargs(int argc, char **argv)
{
    ServerConfig cfg;
    argh::parser cmd({ "-c", "--config" });
    bool hasfile = false;
    cmd.parse(argc, argv);

    for (const auto& it : cmd.params())
    {
        const std::string& k = it.first, & v = it.second;
        if (k == "c" || k == "config")
        {
            if(!hasfile)
                cfg.clear();
            hasfile = true;
            printf("Loading config file: %s\n",  v.c_str());
            loadcfg(cfg, v.c_str());
        }
    }

    if(!cfg.valid())
        bail("Invalid config after processing options. Fix your config file(s).", "");

    return cfg;
}

int main(int argc, char** argv)
{
    handlesigs(sigquit);

    ServerConfig cfg = doargs(argc, argv);

    int ret = server(cfg);

    return ret;
}
