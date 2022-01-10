cmake_minimum_required(VERSION 3.5)
project (cppmyth)

set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Options
OPTION (BUILD_SHARED_LIBS "Build shared libraries." OFF)
if (MSVC)
  # This option must match the settings used in your program, in particular if you
  # are linking statically
  OPTION (STATIC_CRT "Link the static CRT libraries" OFF)
endif ()

if (NOT MSVC)
  OPTION (REQUIRE_CXX_98 "Require standard c++98" OFF)
endif ()

###############################################################################
# set lib version here
set (CPPMYTH_LIB_VERSION "2.14.5")
set (CPPMYTH_LIB_SOVERSION "2")

###############################################################################
# add definitions
if (MSVC)
  add_definitions ("/D_CRT_SECURE_NO_WARNINGS")
  if (STATIC_CRT)
    set (CMAKE_C_FLAGS_RELEASE "/MT")
    set (CMAKE_C_FLAGS_DEBUG "/MTd")
    set (CMAKE_CXX_FLAGS_RELEASE "/MT")
    set (CMAKE_CXX_FLAGS_DEBUG "/MTd")
  endif ()
  set (CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /W3")
  set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /W3")
  set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /W3 /EHsc /nologo")
  set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /W3 /EHsc /nologo")
  if(CMAKE_SYSTEM_NAME STREQUAL WindowsStore)
    set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
    set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /O2")
  else()
    set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Od /RTC1")
    set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Od /RTC1")
  endif()
endif ()

if (NOT MSVC)
  if (NOT CYGWIN)
    add_definitions ("-fPIC")
  endif ()

  add_definitions ("-Wall")

  include(CheckCXXCompilerFlag)
  CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
  if(NOT REQUIRE_CXX_98 AND COMPILER_SUPPORTS_CXX11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
    message(STATUS "Using standard c++11")
  else()
    CHECK_CXX_COMPILER_FLAG("-std=c++98" COMPILER_SUPPORTS_CXX98)
    if(COMPILER_SUPPORTS_CXX98)
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++98")
      message(STATUS "Using standard c++98")
    endif()
    include (CheckLibraryExists)
    include (CheckAtomic)
    if (HAS_BUILTIN_SYNC_ADD_AND_FETCH)
      add_definitions ("-DHAS_BUILTIN_SYNC_ADD_AND_FETCH")
    endif ()
    if (HAS_BUILTIN_SYNC_SUB_AND_FETCH)
      add_definitions ("-DHAS_BUILTIN_SYNC_SUB_AND_FETCH")
    endif ()
  endif ()
endif ()

###############################################################################
# configure
include (CheckFunctionExists)
include (CheckFunctionKeywords)
find_package (Threads REQUIRED)

check_function_exists (timegm CHK_TIMEGM)
if (CHK_TIMEGM)
    set (HAVE_TIMEGM 1)
else ()
    set (HAVE_TIMEGM 0)
endif ()

check_function_exists (localtime_r CHK_LOCALTIME_R)
if (CHK_LOCALTIME_R)
    set (HAVE_LOCALTIME_R 1)
else ()
    set (HAVE_LOCALTIME_R 0)
endif ()

check_function_exists (gmtime_r CHK_GMTIME_R)
if (CHK_GMTIME_R)
    set (HAVE_GMTIME_R 1)
else ()
    set (HAVE_GMTIME_R 0)
endif ()

find_package (ZLIB REQUIRED)
if (ZLIB_FOUND)
    include_directories (${ZLIB_INCLUDE_DIRS})
    set (HAVE_ZLIB 1)
else ()
    set (HAVE_ZLIB 0)
endif ()

set (HAVE_OPENSSL 0)

# Check what the inline keyword is.
check_function_keywords ("inline")
check_function_keywords ("__inline")
check_function_keywords ("__inline__")
if (HAVE_INLINE)
   set (CC_INLINE inline)
elseif (HAVE___INLINE)
   set (CC_INLINE __inline)
elseif (HAVE___INLINE__)
   set (CC_INLINE __inline__)
else ()
   # no inline on this platform
   set (CC_INLINE)
endif ()

# configure the public config file
configure_file (${CMAKE_CURRENT_SOURCE_DIR}/src/cppmyth_config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/src/cppmyth_config.h)

include_directories (${CMAKE_CURRENT_BINARY_DIR}/src/ ${CMAKE_CURRENT_SOURCE_DIR}/src/.)

###############################################################################
# add sources
file (GLOB SRC_FILES
  src/private/mythdto/*.cpp
  src/private/*.c src/private/*.cpp
  src/proto/*.cpp
  src/*.cpp)

file (GLOB OS_SRC_FILES
  src/private/os/threads/threadpool.cpp)

if (MSVC)
  list (APPEND OS_SRC_FILES
    src/private/os/windows/winpthreads.c)
endif ()

set (CPPMYTH_SOURCES
  ${SRC_FILES} ${OS_SRC_FILES})

file (GLOB HDR_FILES
  src/private/mythdto/*.h
  src/private/*.h
  src/proto/*.h
  src/*.h)

file (GLOB OS_HDR_FILES
  src/private/os/threads/*.h)

set (CPPMYTH_HEADERS
  ${HDR_FILES} ${OS_HDR_FILES})

###############################################################################
# add targets
set (cppmyth_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})
if (ZLIB_FOUND)
  list (APPEND cppmyth_LIBRARIES ${ZLIB_LIBRARIES})
endif ()
#if (OPENSSL_FOUND)
#  list (APPEND cppmyth_LIBRARIES ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY})
#endif ()
if (MSVC)
  list (APPEND cppmyth_LIBRARIES ws2_32)
else ()
  list (APPEND cppmyth_LIBRARIES m)
  find_library (LIBRT rt)
  if (LIBRT)
    list (APPEND cppmyth_LIBRARIES rt)
  endif ()
endif ()

add_library (cppmyth STATIC ${CPPMYTH_SOURCES})
target_link_libraries (cppmyth ${cppmyth_LIBRARIES})
