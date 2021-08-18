set(src
    parser.cpp
    parser.h
)

add_library(viewengine ${src})
target_link_libraries(viewengine base)
