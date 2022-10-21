add_executable(testbase testbase.cpp)
target_link_libraries(testbase base alldeps)

add_executable(testparser testparser.cpp)
target_link_libraries(testparser base alldeps)

add_executable(testcontainer testcontainer.cpp)
target_link_libraries(testcontainer base alldeps)

add_executable(testbj testbj.cpp)
target_link_libraries(testbj base alldeps)

add_executable(testbjfuzz testbjfuzz.cpp)
target_link_libraries(testbjfuzz base alldeps)
