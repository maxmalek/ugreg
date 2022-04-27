cmake_minimum_required(VERSION 3.11)
project(ugreg)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

option(RAPIDJSON_SSE42 "Use SSE 4.2 for parsing" FALSE)
option(RAPIDJSON_SSE2 "Use SSE 2 for parsing" TRUE)
option(RAPIDJSON_NEON "Use NEON (ARM) for parsing" TRUE)

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

include_directories(dep)
add_subdirectory(dep)
add_subdirectory(src)
