# FindFFmpeg.cmake
# Finds FFmpeg libraries (avformat, avcodec, avutil, swscale, swresample)
#
# Usage:
#   find_package(FFmpeg REQUIRED)
#   target_link_libraries(mylib FFmpeg::FFmpeg)
#
# Set FFMPEG_ROOT to the FFmpeg installation directory (a shared build with
# include/, lib/ and bin/). The project targets FFmpeg 7.1 (libavcodec 61).

if(NOT FFMPEG_ROOT)
  set(FFMPEG_ROOT "" CACHE PATH "FFmpeg installation root directory")
endif()

if(NOT EXISTS "${FFMPEG_ROOT}/include/libavcodec/version.h")
  message(FATAL_ERROR
    "FFmpeg not found: FFMPEG_ROOT='${FFMPEG_ROOT}' does not contain "
    "include/libavcodec/version.h. Point FFMPEG_ROOT at an FFmpeg 7.1 shared "
    "build, e.g. -DFFMPEG_ROOT=D:/Install/ffmpeg-7.1.1-full_build-shared.")
endif()

# The code relies on FFmpeg 7.1 APIs (e.g. avcodec_get_supported_config,
# libavcodec 61.13); fail here with a clear message instead of in the compiler.
set(_ffmpeg_major_header "${FFMPEG_ROOT}/include/libavcodec/version_major.h")
set(FFMPEG_AVCODEC_VERSION "0.0")
if(EXISTS "${_ffmpeg_major_header}")
  file(STRINGS "${_ffmpeg_major_header}" _ffmpeg_major
       REGEX "^#define LIBAVCODEC_VERSION_MAJOR +[0-9]+")
  file(STRINGS "${FFMPEG_ROOT}/include/libavcodec/version.h" _ffmpeg_minor
       REGEX "^#define LIBAVCODEC_VERSION_MINOR +[0-9]+")
  string(REGEX REPLACE ".* ([0-9]+)$" "\\1" _ffmpeg_major "${_ffmpeg_major}")
  string(REGEX REPLACE ".* ([0-9]+)$" "\\1" _ffmpeg_minor "${_ffmpeg_minor}")
  set(FFMPEG_AVCODEC_VERSION "${_ffmpeg_major}.${_ffmpeg_minor}")
endif()
if(FFMPEG_AVCODEC_VERSION VERSION_LESS 61.13)
  message(FATAL_ERROR
    "FFmpeg at '${FFMPEG_ROOT}' is too old (libavcodec ${FFMPEG_AVCODEC_VERSION}); "
    "this project needs FFmpeg 7.1 or newer (libavcodec >= 61.13).")
elseif(FFMPEG_AVCODEC_VERSION VERSION_GREATER_EQUAL 62)
  message(WARNING
    "FFmpeg at '${FFMPEG_ROOT}' has libavcodec ${FFMPEG_AVCODEC_VERSION}; the "
    "project is only tested with FFmpeg 7.1 (libavcodec 61).")
endif()

find_path(FFMPEG_INCLUDE_DIR
  NAMES libavformat/avformat.h
  PATHS ${FFMPEG_ROOT}/include
  NO_DEFAULT_PATH
)

set(_ffmpeg_components avformat avcodec avutil swscale swresample)
set(_ffmpeg_libs)

foreach(_comp ${_ffmpeg_components})
  string(TOUPPER ${_comp} _COMP_UPPER)
  find_library(FFMPEG_${_COMP_UPPER}_LIBRARY
    NAMES ${_comp}
    PATHS ${FFMPEG_ROOT}/lib
    NO_DEFAULT_PATH
  )
  if(FFMPEG_${_COMP_UPPER}_LIBRARY)
    list(APPEND _ffmpeg_libs ${FFMPEG_${_COMP_UPPER}_LIBRARY})
  endif()
endforeach()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg
  REQUIRED_VARS
    FFMPEG_INCLUDE_DIR
    FFMPEG_AVFORMAT_LIBRARY
    FFMPEG_AVCODEC_LIBRARY
    FFMPEG_AVUTIL_LIBRARY
    FFMPEG_SWSCALE_LIBRARY
    FFMPEG_SWRESAMPLE_LIBRARY
)

if(FFmpeg_FOUND AND NOT TARGET FFmpeg::FFmpeg)
  add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
  set_target_properties(FFmpeg::FFmpeg PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "${_ffmpeg_libs}"
  )
endif()
