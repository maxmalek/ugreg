set(src
    main.cpp
    handler_mxid_v2.cpp
    handler_mxid_v2.h
    mxstore.cpp
    mxstore.h
    mxdefines.cpp
    mxdefines.h
    mxresolv.cpp
    mxresolv.h
    mxhttprequest.cpp
    mxhttprequest.h
    mxtoken.cpp
    mxtoken.h
    mxsources.cpp
    mxsources.h
    mxservices.cpp
    mxservices.h
    mxsearchalgo.cpp
    mxsearchalgo.h
    mxsearch.cpp
    mxsearch.h
    mxvirtual.h
)

set(extra_libs)

if(WIN32)
  set(extra_libs ${extra_libs} dnsapi)
endif()

add_executable(maiden ${src})
target_link_libraries(maiden server ${extra_libs})
postbuild_copy_target_exe(maiden)
