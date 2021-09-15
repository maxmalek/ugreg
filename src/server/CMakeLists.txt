set(src
    main.cpp
    cachetable.h
    config.cpp
    config.h
    webserver.cpp
    webserver.h
    handler_get.cpp
    handler_get.h
    subproc.cpp
    subproc.h
    socketstream.cpp
    socketstream.h
    treefunc.cpp
    treefunc.h
    cachetable.h
    request.cpp
    request.h
    zstream.cpp
    zstream.h
    handler_view.cpp
    handler_view.h
)

add_executable(ugreg ${src})
target_link_libraries(ugreg base viewengine alldeps)
