add_executable(mvp_transcode_cli transcode_cli/main.cc)

target_link_libraries(mvp_transcode_cli PRIVATE mvp_core)

# Copy FFmpeg/mvp_core runtime DLLs next to the CLI tool so it can run
# standalone from build/bin, mirroring mvp_app's post-build steps.
if(WIN32)
  add_custom_command(TARGET mvp_transcode_cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_RUNTIME_DLLS:mvp_transcode_cli>
      $<TARGET_FILE_DIR:mvp_transcode_cli>
    COMMAND_EXPAND_LISTS
  )

  file(GLOB FFMPEG_DLLS "${FFMPEG_ROOT}/bin/*.dll")
  foreach(_dll ${FFMPEG_DLLS})
    add_custom_command(TARGET mvp_transcode_cli POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_dll}"
        $<TARGET_FILE_DIR:mvp_transcode_cli>
    )
  endforeach()
endif()
