# FindFFmpeg.cmake
# Finds FFmpeg libraries (avformat, avcodec, avutil, swscale, swresample)
#
# Usage:
#   find_package(FFmpeg REQUIRED)
#   target_link_libraries(mylib FFmpeg::FFmpeg)
#
# Set FFMPEG_ROOT to the FFmpeg installation directory.

if(NOT FFMPEG_ROOT)
  set(FFMPEG_ROOT "" CACHE PATH "FFmpeg installation root directory")
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
