set(src
    viewparser.cpp
    viewparser.h
    viewexec.cpp
    viewexec.h
    view.cpp
    view.h
    viewmgr.cpp
    viewmgr.h
)

add_library(viewengine ${src})
target_link_libraries(viewengine base)
