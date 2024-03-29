cmake_minimum_required(VERSION 3.11)
project(ugreg)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

set(BUILD_BINDIR "${CMAKE_SOURCE_DIR}/bin")
option(BUILD_COPY_BINARIES "Copy binaries to bin/ subdirs after build" TRUE)

include("${CMAKE_SOURCE_DIR}/cmake/utility.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/BuildType.cmake")


option(RAPIDJSON_SSE42 "Use SSE 4.2 for parsing" FALSE)
option(RAPIDJSON_SSE2 "Use SSE 2 for parsing" TRUE)
option(RAPIDJSON_NEON "Use NEON (ARM) for parsing" TRUE)

if(MSVC)
    option(MSVC_DEBUG_EDIT_AND_CONTINUE "MSVC: Enable edit+continue for debug builds?" TRUE)
    if(MSVC_DEBUG_EDIT_AND_CONTINUE)
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /ZI")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /SAFESEH:NO")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SAFESEH:NO")
    endif()
endif()

if(UNIX)
    add_definitions(-fPIC)
endif()

if(RAPIDJSON_SSE42)
  add_definitions(-DRAPIDJSON_SSE42)
endif()
if(RAPIDJSON_SSE2)
  add_definitions(-DRAPIDJSON_SSE2)
endif()
if(RAPIDJSON_NEON)
  add_definitions(-DRAPIDJSON_NEON)
endif()

include_directories(dep/tomcrypt)
include_directories(dep/brotli/include)
include_directories(dep)
add_subdirectory(dep)
add_subdirectory(src)
