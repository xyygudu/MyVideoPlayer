cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0144 NEW)
project(MyVideoPlayer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for VS Code IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MSVC: propagate INCLUDE and LIB env vars when building outside Developer Command Prompt
if(MSVC)
  if(DEFINED ENV{INCLUDE})
    string(REGEX REPLACE "[;]+" ";" _inc_list "$ENV{INCLUDE}")
    foreach(_inc IN LISTS _inc_list)
      if(EXISTS "${_inc}")
        include_directories(SYSTEM "${_inc}")
      endif()
    endforeach()
  endif()
  if(DEFINED ENV{LIB})
    string(REGEX REPLACE "[;]+" ";" _lib_list "$ENV{LIB}")
    foreach(_lib IN LISTS _lib_list)
      if(EXISTS "${_lib}")
        link_directories("${_lib}")
      endif()
    endforeach()
  endif()
endif()

# Export compile_commands.json for VS Code IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set default output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Append custom cmake module path
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

add_subdirectory(src/media)
add_subdirectory(src/app)
