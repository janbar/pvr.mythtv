project(p8-platform)

cmake_minimum_required(VERSION 2.8.9)
enable_language(CXX)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

find_package(Threads REQUIRED)
include(CheckAtomic.cmake)

set(p8-platform_NAME p8-platform)
set(p8-platform_DESCRIPTION "Platform support library")
set(p8-platform_VERSION_MAJOR 1)
set(p8-platform_VERSION_MINOR 0)
set(p8-platform_VERSION_PATCH 10)

set(CMAKE_POSITION_INDEPENDENT_CODE on)

if(WIN32)
  set(PLAT_SOURCES src/windows/dlfcn-win32.cpp
                   src/windows/os-threads.cpp)
endif()

set(p8-platform_INCLUDE_DIRS "${CMAKE_BINARY_DIR}/include/p8-platform")
IF(WIN32)
  LIST(APPEND p8-platform_INCLUDE_DIRS "${CMAKE_BINARY_DIR}/include/p8-platform/windows")
ENDIF(WIN32)
set(p8-platform_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})

if(NOT ${CORE_SYSTEM_NAME} STREQUAL "")
  if(${CORE_SYSTEM_NAME} STREQUAL "osx" OR ${CORE_SYSTEM_NAME} STREQUAL "ios")
    list(APPEND p8-platform_LIBRARIES "-framework CoreVideo -framework IOKit")
  endif()
endif()

set(SOURCES src/util/fstrcmp.c
            src/util/StringUtils.cpp)

add_library(p8-platform STATIC ${SOURCES} ${PLAT_SOURCES})
target_link_libraries(p8-platform ${p8-platform_LIBRARIES})
set_target_properties(p8-platform PROPERTIES VERSION ${p8-platform_VERSION_MAJOR}.${p8-platform_VERSION_MINOR}.${p8-platform_VERSION_PATCH}
                                              SOVERSION ${p8-platform_VERSION_MAJOR}.0)

#
# Install required headers from p8-platform
configure_file(src/os.h ${CMAKE_BINARY_DIR}/include/p8-platform/os.h COPYONLY)
IF(WIN32)
  configure_file(src/windows/dlfcn-win32.h ${CMAKE_BINARY_DIR}/include/p8-platform/windows/dlfcn-win32.h COPYONLY)
  configure_file(src/windows/os-socket.h ${CMAKE_BINARY_DIR}/include/p8-platform/windows/os-socket.h COPYONLY)
  configure_file(src/windows/os-threads.h ${CMAKE_BINARY_DIR}/include/p8-platform/windows/os-threads.h COPYONLY)
  configure_file(src/windows/os-types.h ${CMAKE_BINARY_DIR}/include/p8-platform/windows/os-types.h COPYONLY)
ELSE(WIN32)
  configure_file(src/posix/os-socket.h ${CMAKE_BINARY_DIR}/include/p8-platform/posix/os-socket.h COPYONLY)
  configure_file(src/posix/os-threads.h ${CMAKE_BINARY_DIR}/include/p8-platform/posix/os-threads.h COPYONLY)
  configure_file(src/posix/os-types.h ${CMAKE_BINARY_DIR}/include/p8-platform/posix/os-types.h COPYONLY)
ENDIF(WIN32)
configure_file(src/sockets/cdevsocket.h ${CMAKE_BINARY_DIR}/include/p8-platform/sockets/cdevsocket.h COPYONLY)
configure_file(src/sockets/socket.h ${CMAKE_BINARY_DIR}/include/p8-platform/sockets/socket.h COPYONLY)
configure_file(src/sockets/tcp.h ${CMAKE_BINARY_DIR}/include/p8-platform/sockets/tcp.h COPYONLY)
configure_file(src/threads/atomics.h ${CMAKE_BINARY_DIR}/include/p8-platform/threads/atomics.h COPYONLY)
configure_file(src/threads/mutex.h ${CMAKE_BINARY_DIR}/include/p8-platform/threads/mutex.h COPYONLY)
configure_file(src/threads/threads.h ${CMAKE_BINARY_DIR}/include/p8-platform/threads/threads.h COPYONLY)
configure_file(src/util/atomic.h ${CMAKE_BINARY_DIR}/include/p8-platform/util/atomic.h COPYONLY)
configure_file(src/util/buffer.h ${CMAKE_BINARY_DIR}/include/p8-platform/util/buffer.h COPYONLY)
configure_file(src/util/StringUtils.h ${CMAKE_BINARY_DIR}/include/p8-platform/util/StringUtils.h COPYONLY)
configure_file(src/util/StdString.h ${CMAKE_BINARY_DIR}/include/p8-platform/util/StdString.h COPYONLY)
configure_file(src/util/timeutils.h ${CMAKE_BINARY_DIR}/include/p8-platform/util/timeutils.h COPYONLY)
configure_file(src/util/util.h ${CMAKE_BINARY_DIR}/include/p8-platform/util/util.h COPYONLY)

