# This thing pulls in too much shit, so just do the bare essentials below instead
#add_subdirectory(civetweb)

# via https://github.com/civetweb/civetweb/blob/master/docs/Building.md
add_definitions(-DNO_SSL)   # listen on localhost and use a reverse proxy if ssl is needed
add_definitions(-DNO_CGI)   # not required
add_definitions(-DUSE_IPV6)

set(src
    luaalloc.c
    luaalloc.h
    civetweb/civetweb.c
    civetweb/civetweb.h
    subprocess.h
    strpool.h
    impls.cpp
)

add_library(alldeps ${src})
#target_link_libraries(alldeps)
