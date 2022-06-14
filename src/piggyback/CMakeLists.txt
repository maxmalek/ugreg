set(src
    main.cpp
    handler_fwd.cpp
    handler_fwd.h
)


add_executable(piggyback ${src})
target_link_libraries(piggyback server)
