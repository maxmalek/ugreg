set(src
    types.h
    datatree.cpp
    datatree.h
    util.cpp
    util.h
    variant.cpp
    variant.h
    main.cpp
    cachetable.h
    config.cpp
    config.h
    webserver.cpp
    webserver.h
    json_in.cpp
    json_in.h
    json_out.cpp
    json_out.h
    query.cpp
    query.h
    accessor.cpp
    accessor.h
    handler_get.cpp
    handler_get.h
    subproc.cpp
    subproc.h
    treeiter.h
    jsonstreamwrapper.cpp
    jsonstreamwrapper.h
    treemem.cpp
    treemem.h
    mem.h
)

add_executable(ugreg ${src})
target_link_libraries(ugreg alldeps)
