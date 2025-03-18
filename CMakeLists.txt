cmake_minimum_required(VERSION 3.14)
project(mjpeg_server VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # Export compile_commands.json for LSP

# Options
option(MJPEG_SERVER_BUILD_EXAMPLES "Build the examples" ON)
option(MJPEG_SERVER_INSTALL "Generate installation target" ON)

# Find OpenCV
find_package(OpenCV REQUIRED)

# Define the library
add_library(mjpeg_server 
    src/mjpeg_server.cpp
)

# Set up include directories - FIXED: added src to private includes
target_include_directories(mjpeg_server
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${OpenCV_INCLUDE_DIRS}
)

# Link dependencies
target_link_libraries(mjpeg_server
    PUBLIC
        ${OpenCV_LIBS}
    PRIVATE
        pthread
)

# Create an interface target to allow easier inclusion as a subproject
add_library(mjpeg_server::mjpeg_server ALIAS mjpeg_server)

# Examples
if(MJPEG_SERVER_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Installation
if(MJPEG_SERVER_INSTALL)
    include(GNUInstallDirs)
    
    install(
        TARGETS mjpeg_server
        EXPORT mjpeg_server-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(
        DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    # Export the targets
    install(
        EXPORT mjpeg_server-targets
        FILE mjpeg_server-targets.cmake
        NAMESPACE mjpeg_server::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mjpeg_server
    )

    # Create and install config file
    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/mjpeg_server-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/mjpeg_server-config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mjpeg_server
    )

    # Create and install version file
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/mjpeg_server-config-version.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(
        FILES
            ${CMAKE_CURRENT_BINARY_DIR}/mjpeg_server-config.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/mjpeg_server-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mjpeg_server
    )
endif()
