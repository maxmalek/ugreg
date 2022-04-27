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

set(libs)

if(WIN32)
  set(libs ws2_32)
endif()

add_executable(extrond ${src})
target_link_libraries(extrond server ${libs})
