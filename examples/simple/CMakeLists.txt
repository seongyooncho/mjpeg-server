add_executable(mjpeg_server_example main.cpp)

target_link_libraries(mjpeg_server_example PRIVATE mjpeg_server)

# macOS specific settings for camera access - using local Info.plist
if(APPLE)
    set_target_properties(mjpeg_server_example PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    )
endif()
