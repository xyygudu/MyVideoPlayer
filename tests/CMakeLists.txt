include(FetchContent)
include(GoogleTest)

# GoogleTest, pinned by version and hash.
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/releases/download/v1.15.2/googletest-1.15.2.tar.gz
  URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# Same (dynamic) MSVC runtime as the rest of the project; no gmock, no install.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# --- Test media: generated with the FFmpeg CLI that ships with FFMPEG_ROOT ---
find_program(MVP_FFMPEG_EXE ffmpeg HINTS "${FFMPEG_ROOT}/bin" NO_DEFAULT_PATH)
if(NOT MVP_FFMPEG_EXE)
  message(FATAL_ERROR
    "The tests generate their media with ffmpeg, but '${FFMPEG_ROOT}/bin' has no ffmpeg "
    "executable. Use a full FFmpeg build or configure with -DMVP_BUILD_TESTS=OFF.")
endif()

set(MVP_TEST_MEDIA_DIR "${CMAKE_CURRENT_BINARY_DIR}/media")
set(MVP_TEST_MEDIA_STAMP "${MVP_TEST_MEDIA_DIR}/media.stamp")
add_custom_command(
  OUTPUT "${MVP_TEST_MEDIA_STAMP}"
  COMMAND "${CMAKE_COMMAND}" "-DFFMPEG=${MVP_FFMPEG_EXE}" "-DOUT_DIR=${MVP_TEST_MEDIA_DIR}"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/generate_test_media.cmake"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/generate_test_media.cmake"
  COMMENT "Generating test media with ffmpeg"
  VERBATIM
)
add_custom_target(mvp_test_media DEPENDS "${MVP_TEST_MEDIA_STAMP}")

# --- Test executable: links the core objects directly to reach internal classes ---
file(GLOB MVP_TEST_SOURCES CONFIGURE_DEPENDS *.cc *.h)
add_executable(mvp_tests ${MVP_TEST_SOURCES})
add_dependencies(mvp_tests mvp_test_media)
target_link_libraries(mvp_tests PRIVATE mvp_core_objects GTest::gtest)
target_compile_definitions(mvp_tests PRIVATE
  MVP_CORE_STATIC_DEFINE
  MVP_TEST_MEDIA_DIR="${MVP_TEST_MEDIA_DIR}"
  MVP_TEST_LOG_FILE="${CMAKE_CURRENT_BINARY_DIR}/mvp_tests.log"
)

if(WIN32)
  add_custom_command(TARGET mvp_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_RUNTIME_DLLS:mvp_tests>
      $<TARGET_FILE_DIR:mvp_tests>
    COMMAND_EXPAND_LISTS
  )
  file(GLOB MVP_FFMPEG_DLLS "${FFMPEG_ROOT}/bin/*.dll")
  add_custom_command(TARGET mvp_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MVP_FFMPEG_DLLS} $<TARGET_FILE_DIR:mvp_tests>
  )
endif()

# Listed at test time, once the runtime DLLs sit next to the executable.
gtest_discover_tests(mvp_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 180)
