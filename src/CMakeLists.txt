include_directories(.)
include_directories(base)

option(BUILD_TESTS "Build test files?" TRUE)

add_subdirectory(base)
add_subdirectory(view)
add_subdirectory(server)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
