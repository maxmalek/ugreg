set(src
    main.cpp
    cachetable.h
    config.cpp
    config.h
    webserver.cpp
    webserver.h
    handler_get.cpp
    handler_get.h
    socketstream.cpp
    socketstream.h
    cachetable.h
    request.cpp
    request.h
    zstream.cpp
    zstream.h
    handler_view.cpp
    handler_view.h
    handler_debug.cpp
    handler_debug.h
)

add_executable(ugreg ${src})
target_link_libraries(ugreg base alldeps)
