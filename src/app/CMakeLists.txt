find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(spdlog REQUIRED)
qt_standard_project_setup()

file(GLOB_RECURSE MVP_APP_SOURCES src/*.cc)
file(GLOB_RECURSE MVP_APP_HEADERS src/*.h)

qt_add_executable(mvp_app ${MVP_APP_SOURCES} ${MVP_APP_HEADERS})

target_link_libraries(mvp_app PRIVATE
  mvp_core
  Qt6::Widgets
  spdlog::spdlog
)

# Copy Qt/SDL DLLs on Windows for debugging convenience
if(WIN32)
  add_custom_command(TARGET mvp_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_RUNTIME_DLLS:mvp_app>
      $<TARGET_FILE_DIR:mvp_app>
    COMMAND_EXPAND_LISTS
  )

  # Copy FFmpeg DLLs (declared as INTERFACE, not picked up by TARGET_RUNTIME_DLLS)
  file(GLOB FFMPEG_DLLS "${FFMPEG_ROOT}/bin/*.dll")
  foreach(_dll ${FFMPEG_DLLS})
    add_custom_command(TARGET mvp_app POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_dll}"
        $<TARGET_FILE_DIR:mvp_app>
    )
  endforeach()

  # Copy Qt platform plugin (qwindows.dll) so the app can start
  add_custom_command(TARGET mvp_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:mvp_app>/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_PROPERTY:Qt6::QWindowsIntegrationPlugin,LOCATION_$<CONFIG>>"
      "$<TARGET_FILE_DIR:mvp_app>/platforms/"
  )
endif()
