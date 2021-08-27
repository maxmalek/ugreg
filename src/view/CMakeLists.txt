set(src
    viewparser.cpp
    viewparser.h
    viewexec.cpp
    viewexec.h
)

add_library(viewengine ${src})
target_link_libraries(viewengine base)
