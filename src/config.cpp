#include "config.h"

// make the default values sane for testing and not a security issue.
// We're not MongoDB.
ServerConfig::ServerConfig()
    : listenaddr("127.0.0.1")
    , listenport(8080)
{}
