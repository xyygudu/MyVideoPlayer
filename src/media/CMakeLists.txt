find_package(FFmpeg REQUIRED)
# 3.2 is the first stable SDL3 release: texture colorspace properties,
# SDL_GetCurrentRenderOutputSize and the D3D11 device properties used here.
find_package(SDL3 3.2 REQUIRED CONFIG)
find_package(spdlog REQUIRED)

file(GLOB_RECURSE MVP_CORE_SOURCES CONFIGURE_DEPENDS *.cc)
file(GLOB_RECURSE MVP_CORE_HEADERS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/include/mvp/*.h)

# Generated headers live in the build tree, never in the sources: each build
# tree (Ninja, Visual Studio) generates its own copy.
set(MVP_GENERATED_INCLUDE_DIR ${PROJECT_BINARY_DIR}/generated/include)

# The core compiles once into an object library: the mvp_core DLL is linked
# from it, and so are the tests, which need the internal (unexported) classes.
add_library(mvp_core_objects OBJECT ${MVP_CORE_SOURCES} ${MVP_CORE_HEADERS})
target_include_directories(mvp_core_objects
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${MVP_GENERATED_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(mvp_core_objects
  PUBLIC
    FFmpeg::FFmpeg
    SDL3::SDL3
    spdlog::spdlog
)
target_compile_definitions(mvp_core_objects PRIVATE mvp_core_EXPORTS)

add_library(mvp_core SHARED $<TARGET_OBJECTS:mvp_core_objects>)

target_include_directories(mvp_core
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${MVP_GENERATED_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# FFmpeg stays private: no public header (include/mvp) exposes its types.
target_link_libraries(mvp_core
  PRIVATE
    FFmpeg::FFmpeg
    SDL3::SDL3
    spdlog::spdlog
)

# --- GPU video pass shaders: HLSL compiled at build time into bytecode headers ---
if(WIN32)
  find_program(MVP_FXC fxc
    HINTS
      "$ENV{SDK_ROOT}/bin/$ENV{SDK_VER}/x64"
      "$ENV{WindowsSdkVerBinPath}x64"
  )
  if(NOT MVP_FXC)
    message(FATAL_ERROR
      "fxc.exe (HLSL compiler from the Windows 10/11 SDK) was not found. It is "
      "required to build the GPU video passes. Install the Windows SDK or add "
      "its bin/<version>/x64 directory to PATH.")
  endif()

  set(MVP_SHADER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/gpu/shaders)
  set(MVP_SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/gpu/shaders)
  set(MVP_SHADER_HEADERS)

  # Compiles one entry point of <file> to ${MVP_SHADER_OUT}/<variable>.h
  function(mvp_add_shader file entry profile variable)
    set(out ${MVP_SHADER_OUT}/${variable}.h)
    add_custom_command(
      OUTPUT ${out}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${MVP_SHADER_OUT}
      COMMAND ${MVP_FXC} /nologo /O3 /T ${profile} /E ${entry} /Vn ${variable}
              /Fh ${out} ${MVP_SHADER_SRC}/${file}
      DEPENDS ${MVP_SHADER_SRC}/${file} ${MVP_SHADER_SRC}/video_pass_common.hlsli
      COMMENT "fxc ${file} (${entry})"
      VERBATIM
    )
    set(MVP_SHADER_HEADERS ${MVP_SHADER_HEADERS} ${out} PARENT_SCOPE)
  endfunction()

  mvp_add_shader(fullscreen_vs.hlsl   VSMain   vs_4_0 kFullscreenVs)
  mvp_add_shader(color_adjust_ps.hlsl PSLuma   ps_4_0 kColorAdjustLumaPs)
  mvp_add_shader(color_adjust_ps.hlsl PSChroma ps_4_0 kColorAdjustChromaPs)
  mvp_add_shader(affine_remap_ps.hlsl PSLuma   ps_4_0 kAffineRemapLumaPs)
  mvp_add_shader(affine_remap_ps.hlsl PSChroma ps_4_0 kAffineRemapChromaPs)

  target_sources(mvp_core_objects PRIVATE ${MVP_SHADER_HEADERS})
  target_include_directories(mvp_core_objects PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()

# Export symbols on Windows (mvp_core_EXPORTS is set on mvp_core_objects above;
# consumers that link the objects statically define MVP_CORE_STATIC_DEFINE).
include(GenerateExportHeader)
generate_export_header(mvp_core
  EXPORT_FILE_NAME ${MVP_GENERATED_INCLUDE_DIR}/mvp/export.h
)
