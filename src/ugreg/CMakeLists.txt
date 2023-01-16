set(src
    main.cpp
    handler_get.cpp
    handler_get.h
    handler_view.cpp
    handler_view.h
    handler_debug.cpp
    handler_debug.h
)

add_executable(ugreg ${src})
target_link_libraries(ugreg server)
postbuild_copy_target_exe(ugreg)
