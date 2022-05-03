set(src
    main.cpp
    handler_mxid_v2.cpp
    handler_mxid_v2.h
    mxstore.cpp
    mxstore.h
)

add_executable(maiden ${src})
target_link_libraries(maiden server)
