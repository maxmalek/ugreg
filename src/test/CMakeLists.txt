add_executable(testbase testbase.cpp)
target_link_libraries(testbase base alldeps)

add_executable(testparser testparser.cpp)
target_link_libraries(testparser viewengine base alldeps)
