set(src
    main.cpp
    sisclient.cpp
    sisclient.h
    sissocket.cpp
    sissocket.h
    sisdevice.cpp
    sisdevice.h
    handler_status.cpp
    handler_status.h
    sisluafunc.cpp
    sisluafunc.h
    handler_ctrl.cpp
    handler_ctrl.h
)

add_executable(extrond ${src})
target_link_libraries(extrond server)
postbuild_copy_target_exe(extrond)
