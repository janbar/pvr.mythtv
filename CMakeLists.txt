project(pvr.mythtv)

cmake_minimum_required(VERSION 2.8.12)

set(APP_VERSION_MAJOR "17" CACHE STRING "Application version major")
set(APP_VERSION_MINOR "0" CACHE STRING "Application verison minor")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/cmake)

# Make sure CORE_SYSTEM_NAME is set
if(NOT CORE_SYSTEM_NAME)
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(CORE_SYSTEM_NAME "osx")
  else()
    string(TOLOWER ${CMAKE_SYSTEM_NAME} CORE_SYSTEM_NAME)
  endif()
endif()
include(addon-helpers)

enable_language(CXX)
enable_language(C)

include_directories(${CMAKE_BINARY_DIR}/include
                    ${CMAKE_BINARY_DIR}/include/kodi
                    ${PROJECT_SOURCE_DIR}/lib/cppmyth/src)

add_definitions(-DUSE_DEMUX -D__STDC_FORMAT_MACROS -DBUILD_KODI_ADDON)

if(NOT WIN32)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -Wall -std=c++11")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -Wall")
else()
  add_definitions(-DTARGET_WINDOWS -D_WINSOCKAPI_
    -D_USE_32BIT_TIME_T -D_CRT_SECURE_NO_WARNINGS
    -D_SCL_SECURE_NO_WARNINGS -D__STDC_CONSTANT_MACROS)
  set(CMAKE_CXX_FLAGS_RELEASE "/MT /W3 /Od /RTC1 /EHsc")
  set(CMAKE_CXX_FLAGS_DEBUG "/MTd /W3 /Od /RTC1 /EHsc")
  set(CMAKE_C_FLAGS_RELEASE "/MT /W3")
  set(CMAKE_C_FLAGS_DEBUG "/MTd /W3")
endif()

file (GLOB MYTHTV_SOURCES
  src/demuxer/*.cpp src/demuxer/*.h
  src/cppmyth/*.cpp src/cppmyth/*.h
  src/*.cpp src/*.h)

add_subdirectory(lib/cppmyth)
add_subdirectory(lib/kodi)
add_subdirectory(lib/p8-platform)

set(DEPLIBS p8-platform cppmyth)
if(WIN32)
  list(APPEND DEPLIBS ws2_32)
else()
  list(APPEND DEPLIBS m dl)
endif()

if(NOT WIN32 AND (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX))
  if(HAS_BUILTIN_SYNC_ADD_AND_FETCH)
    add_definitions("-DHAS_BUILTIN_SYNC_ADD_AND_FETCH")
  endif()
  if(HAS_BUILTIN_SYNC_SUB_AND_FETCH)
    add_definitions("-DHAS_BUILTIN_SYNC_SUB_AND_FETCH")
  endif()
endif()

set(BUILD_SHARED_LIBS ON)
build_addon(pvr.mythtv MYTHTV DEPLIBS)

include(CPack)
