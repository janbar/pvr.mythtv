project(kodidepends)

cmake_minimum_required (VERSION 2.6 FATAL_ERROR)
cmake_policy (VERSION 2.6)

set(APP_VERSION_MAJOR "17" CACHE string "Application version major")
set(APP_VERSION_MINOR "0" CACHE string "Application version minor")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

#
# Install required headers from kodi depends
IF(WIN32)
  configure_file(pvr/dlfcn-win32.h ${CMAKE_BINARY_DIR}/include/kodi/dlfcn-win32.h COPYONLY)
ENDIF(WIN32)
configure_file(pvr/libXBMC_addon.h ${CMAKE_BINARY_DIR}/include/kodi/libXBMC_addon.h COPYONLY)
configure_file(pvr/libXBMC_codec.h ${CMAKE_BINARY_DIR}/include/kodi/libXBMC_codec.h COPYONLY)
configure_file(pvr/libKODI_guilib.h ${CMAKE_BINARY_DIR}/include/kodi/libKODI_guilib.h COPYONLY)
configure_file(pvr/libXBMC_pvr.h ${CMAKE_BINARY_DIR}/include/kodi/libXBMC_pvr.h COPYONLY)
configure_file(pvr/DVDDemuxPacket.h ${CMAKE_BINARY_DIR}/include/kodi/DVDDemuxPacket.h COPYONLY)
configure_file(pvr/xbmc_addon_cpp_dll.h ${CMAKE_BINARY_DIR}/include/kodi/xbmc_addon_cpp_dll.h COPYONLY)
configure_file(pvr/xbmc_addon_dll.h ${CMAKE_BINARY_DIR}/include/kodi/xbmc_addon_dll.h COPYONLY)
configure_file(pvr/xbmc_addon_types.h ${CMAKE_BINARY_DIR}/include/kodi/xbmc_addon_types.h COPYONLY)
configure_file(pvr/xbmc_codec_types.h ${CMAKE_BINARY_DIR}/include/kodi/xbmc_codec_types.h COPYONLY)
configure_file(pvr/xbmc_epg_types.h ${CMAKE_BINARY_DIR}/include/kodi/xbmc_epg_types.h COPYONLY)
configure_file(pvr/xbmc_pvr_dll.h ${CMAKE_BINARY_DIR}/include/kodi/xbmc_pvr_dll.h COPYONLY)
configure_file(pvr/xbmc_pvr_types.h ${CMAKE_BINARY_DIR}/include/kodi/xbmc_pvr_types.h COPYONLY)
configure_file(pvr/xbmc_scr_dll.h ${CMAKE_BINARY_DIR}/include/kodi/xbmc_scr_dll.h COPYONLY)
configure_file(pvr/xbmc_scr_types.h ${CMAKE_BINARY_DIR}/include/kodi/xbmc_scr_types.h COPYONLY)
configure_file(pvr/xbmc_vis_dll.h ${CMAKE_BINARY_DIR}/include/kodi/xbmc_vis_dll.h COPYONLY)
configure_file(pvr/xbmc_vis_types.h ${CMAKE_BINARY_DIR}/include/kodi/xbmc_vis_types.h COPYONLY)
configure_file(pvr/IFileTypes.h ${CMAKE_BINARY_DIR}/include/kodi/IFileTypes.h COPYONLY)

