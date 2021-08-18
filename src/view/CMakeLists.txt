set(src
    viewparser.cpp
    viewparser.h
)

add_library(viewengine ${src})
target_link_libraries(viewengine base)
