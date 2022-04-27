# This thing pulls in too much shit, so just do the bare essentials below instead
#add_subdirectory(civetweb)

set(extra_libs)

option(USE_MBEDTLS "Enable SSL/TLS support via mbedTLS" TRUE)
if(USE_MBEDTLS)
    find_package(MbedTLS REQUIRED)
    include_directories(${MBEDTLS_INCLUDE_DIRS})
    add_definitions(-DUSE_MBEDTLS)
    set(extra_libs ${extra_libs} ${MBEDTLS_LIBRARIES})
    message(STATUS "Using mbedTLS libs: ${MBEDTLS_LIBRARIES}")
else()
    add_definitions(-DNO_SSL)   # listen on localhost and use a reverse proxy if ssl is needed
endif()

# via https://github.com/civetweb/civetweb/blob/master/docs/Building.md
add_definitions(-DNO_CGI)   # not required
add_definitions(-DUSE_IPV6)
add_definitions(-DNO_FILES)
add_definitions(-DNO_FILESYSTEMS) # not required. makes request handling faster, too. See check_authorization() if in doubt.
add_definitions(-DMG_EXTERNAL_FUNCTION_mg_cry_internal_impl) # required for NO_FILESYSTEMS
add_definitions(-DMG_EXTERNAL_FUNCTION_log_access) # required for NO_FILESYSTEMS
#add_definitions(-DALTERNATIVE_QUEUE) # doesn't seem to do anything good

# for miniz: disable extra stuff we don't need
add_definitions(-DMINIZ_NO_TIME)
add_definitions(-DMINIZ_NO_ARCHIVE_APIS)
add_definitions(-DMINIZ_NO_ARCHIVE_WRITING_APIS)
add_definitions(-DMINIZ_NO_ZLIB_COMPATIBLE_NAMES)
add_definitions(-DMINIZ_NO_STDIO)

add_definitions(-DLA_TRACK_STATS)

set(src
    luaalloc.c
    luaalloc.h
    civetweb/civetweb.c
    civetweb/civetweb.h
    external_mg_cry_internal_impl.inl  # civetweb stub
    external_log_access.inl            # civetweb stub
    subprocess.h
    strpool.h
    argh.h
    impls.cpp
    miniz.c
    miniz.h
    upgrade_mutex.cpp
    upgrade_mutex.h
    minicoro.h
)

add_library(alldeps ${src})

if(UNIX)
    set(extra_libs ${extra_libs} pthread dl)
endif()

target_link_libraries(alldeps ${extra_libs})

add_subdirectory(lua)
