set(src
    main.cpp
)

add_executable(extrond ${src})
target_link_libraries(extrond server)
