set(src
    config.cpp
    config.h
    cachetable.h
    request.cpp
    request.h
    socketstream.cpp
    socketstream.h
    webserver.cpp
    webserver.h
    zstream.cpp
    zstream.h
    serverutil.cpp
    serverutil.h
    responseformat.cpp
    responseformat.h
    brstream.cpp
    brstream.h
)

add_library(server ${src})
target_link_libraries(server base alldeps)
