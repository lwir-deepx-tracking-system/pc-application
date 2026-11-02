cmake_minimum_required(VERSION 3.25)

project(pc-application LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_standard_project_setup()
option(BUILD_TESTING "Build application tests" OFF)

qt_add_library(control-client STATIC
    src/control/ControlProtocol.cpp src/control/ControlProtocol.h
    src/control/ControlClient.cpp src/control/ControlClient.h src/control/ControlSettings.h)
target_include_directories(control-client PUBLIC src)
target_link_libraries(control-client PUBLIC Qt6::Core Qt6::Network)

qt_add_library(manual-input STATIC src/control/ManualInputHandler.cpp src/control/ManualInputHandler.h)
target_include_directories(manual-input PUBLIC src)
target_link_libraries(manual-input PUBLIC Qt6::Widgets)

find_package(yaml-cpp 0.8 CONFIG QUIET)
if(NOT yaml-cpp_FOUND)
    include(FetchContent)
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "Build yaml-cpp tests" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "Build yaml-cpp tools" FORCE)
    set(YAML_CPP_INSTALL OFF CACHE BOOL "Install yaml-cpp" FORCE)
    set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared yaml-cpp" FORCE)
    FetchContent_Declare(yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG f7320141120f720aecc4c32be25586e7da9eb978)
    block()
        # Use supported compatibility policies for yaml-cpp 0.8 on recent CMake.
        set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
        FetchContent_MakeAvailable(yaml-cpp)
    endblock()
endif()

add_library(app-config STATIC src/config/AppConfig.cpp src/config/AppConfig.h src/video/VideoCodec.h)
target_include_directories(app-config PUBLIC src)
target_link_libraries(app-config PUBLIC Qt6::Core PRIVATE yaml-cpp::yaml-cpp)

if(WIN32)
    set(GSTREAMER_ROOT "$ENV{GSTREAMER_1_0_ROOT_MSVC_X86_64}" CACHE PATH "GStreamer MSVC x64 installation")
    if(NOT GSTREAMER_ROOT)
        foreach(candidate "$ENV{LOCALAPPDATA}/Programs/gstreamer/1.0/msvc_x86_64"
                          "$ENV{ProgramFiles}/gstreamer/1.0/msvc_x86_64" "C:/gstreamer/1.0/msvc_x86_64")
            if(EXISTS "${candidate}/bin/pkg-config.exe")
                set(GSTREAMER_ROOT "${candidate}" CACHE PATH "GStreamer MSVC x64 installation" FORCE)
                break()
            endif()
        endforeach()
    endif()
    find_program(PKG_CONFIG_EXECUTABLE pkg-config HINTS "${GSTREAMER_ROOT}/bin" REQUIRED)
    set(ENV{PKG_CONFIG_PATH} "${GSTREAMER_ROOT}/lib/pkgconfig;$ENV{PKG_CONFIG_PATH}")
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET
    gstreamer-1.0>=1.22 gstreamer-app-1.0 gstreamer-video-1.0 gstreamer-rtp-1.0)

qt_add_library(video-receiver STATIC src/video/VideoReceiver.cpp src/video/VideoReceiver.h
    src/video/VideoPipeline.cpp src/video/VideoPipeline.h
    src/video/VideoTrace.cpp src/video/VideoTrace.h)
target_include_directories(video-receiver PUBLIC src)
target_link_libraries(video-receiver PUBLIC Qt6::Gui PkgConfig::GSTREAMER)

qt_add_library(metadata-receiver STATIC
    src/metadata/TrackingMetadata.cpp src/metadata/TrackingMetadata.h
    src/metadata/MetadataReceiver.cpp src/metadata/MetadataReceiver.h
    src/metadata/FrameSynchronizer.cpp src/metadata/FrameSynchronizer.h)
target_include_directories(metadata-receiver PUBLIC src)
target_link_libraries(metadata-receiver PUBLIC Qt6::Gui Qt6::Network)

qt_add_library(application-controller STATIC src/app/ApplicationController.cpp src/app/ApplicationController.h)
target_include_directories(application-controller PUBLIC src)
target_link_libraries(application-controller PUBLIC video-receiver metadata-receiver control-client)

function(deploy_qt_runtime target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND Qt6::windeployqt --$<IF:$<CONFIG:Debug>,debug,release> --no-translations "$<TARGET_FILE:${target}>"
            VERBATIM)
    endif()
endfunction()

function(deploy_runtime target)
    deploy_qt_runtime(${target})
    if(WIN32)
        file(GLOB gst_dlls "${GSTREAMER_ROOT}/bin/*.dll")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${gst_dlls} "$<TARGET_FILE_DIR:${target}>"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/gstreamer-1.0"
            VERBATIM)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GSTREAMER_ROOT}/libexec/gstreamer-1.0/gst-plugin-scanner.exe"
                "$<TARGET_FILE_DIR:${target}>" VERBATIM)
        foreach(plugin coreelements app udp rtp rtpmanager jpeg jpegformat videoconvertscale videoparsersbad libav x264)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${GSTREAMER_ROOT}/lib/gstreamer-1.0/gst${plugin}.dll"
                    "$<TARGET_FILE_DIR:${target}>/gstreamer-1.0" VERBATIM)
        endforeach()
    endif()
endfunction()

qt_add_library(application-ui STATIC
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/VideoWidget.cpp
    src/ui/VideoWidget.h
    src/ui/ClassLabels.cpp
    src/ui/ClassLabels.h
)
target_include_directories(application-ui PUBLIC src)
target_link_libraries(application-ui PUBLIC Qt6::Widgets control-client)

qt_add_executable(pc-application src/main.cpp src/app/GuiPreview.cpp src/app/GuiPreview.h)

target_link_libraries(pc-application
    PRIVATE
        Qt6::Widgets
        Qt6::Network
        video-receiver
        metadata-receiver
        app-config
        application-controller
        manual-input
        application-ui
)

deploy_runtime(pc-application)
add_custom_command(TARGET pc-application POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pc-application>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/pc-application.yaml"
        "$<TARGET_FILE_DIR:pc-application>/config/pc-application.yaml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/class_names.txt"
        "$<TARGET_FILE_DIR:pc-application>/class_names.txt" VERBATIM)

qt_add_executable(mock-video-sender tools/mock/video/main.cpp)
target_link_libraries(mock-video-sender PRIVATE video-receiver Qt6::Network)
deploy_runtime(mock-video-sender)

qt_add_library(mock-control STATIC tools/mock/control/MockControlServer.cpp tools/mock/control/MockControlServer.h)
target_include_directories(mock-control PUBLIC tools/mock/control)
target_link_libraries(mock-control PUBLIC control-client)
qt_add_executable(mock-control-server tools/mock/control/main.cpp)
set_target_properties(mock-control-server PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(mock-control-server PRIVATE mock-control)
deploy_qt_runtime(mock-control-server)

if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(video-integration-test tests/integration/VideoReceiverTest.cpp)
    set_target_properties(video-integration-test PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(video-integration-test PRIVATE application-controller application-ui app-config Qt6::Widgets Qt6::Test)
    add_dependencies(video-integration-test pc-application mock-video-sender)
    deploy_runtime(video-integration-test)
    add_test(NAME video-integration COMMAND video-integration-test -o -,txt)

    qt_add_executable(control-test tests/control/ControlTest.cpp)
    set_target_properties(control-test PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(control-test PRIVATE application-controller application-ui manual-input mock-control app-config Qt6::Test)
    add_dependencies(control-test pc-application mock-control-server)
    deploy_runtime(control-test)
    add_test(NAME control COMMAND control-test -o -,txt)
    set_tests_properties(control video-integration PROPERTIES RUN_SERIAL TRUE)
endif()
