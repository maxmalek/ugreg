include_directories(.)
include_directories(base)
include_directories(server)

option(BUILD_TESTS "Build test files?" TRUE)

add_subdirectory(base)
add_subdirectory(server)
add_subdirectory(ugreg)
add_subdirectory(extrond)
add_subdirectory(maiden)
add_subdirectory(piggyback)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
