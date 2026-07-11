find_package(FFmpeg REQUIRED)
find_package(SDL3 REQUIRED CONFIG)
find_package(spdlog REQUIRED)

file(GLOB_RECURSE MVP_CORE_SOURCES *.cc)
file(GLOB_RECURSE MVP_CORE_HEADERS ${PROJECT_SOURCE_DIR}/include/mvp/*.h)

add_library(mvp_core SHARED ${MVP_CORE_SOURCES} ${MVP_CORE_HEADERS})

target_include_directories(mvp_core
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(mvp_core
  PUBLIC
    FFmpeg::FFmpeg
  PRIVATE
    SDL3::SDL3
    spdlog::spdlog
)

# Export symbols on Windows
include(GenerateExportHeader)
generate_export_header(mvp_core
  EXPORT_FILE_NAME ${PROJECT_SOURCE_DIR}/include/mvp/export.h
)

target_compile_definitions(mvp_core PRIVATE mvp_core_EXPORTS)
