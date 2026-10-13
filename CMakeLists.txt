cmake_minimum_required(VERSION 3.25)

project(pc-application LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_standard_project_setup()

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
    src/video/VideoTrace.cpp src/video/VideoTrace.h)
target_include_directories(video-receiver PUBLIC src)
target_link_libraries(video-receiver PUBLIC Qt6::Gui PkgConfig::GSTREAMER)

function(deploy_runtime target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND Qt6::windeployqt --$<IF:$<CONFIG:Debug>,debug,release> --no-translations "$<TARGET_FILE:${target}>"
            VERBATIM)
        file(GLOB gst_dlls "${GSTREAMER_ROOT}/bin/*.dll")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${gst_dlls} "$<TARGET_FILE_DIR:${target}>"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/gstreamer-1.0"
            VERBATIM)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GSTREAMER_ROOT}/libexec/gstreamer-1.0/gst-plugin-scanner.exe"
                "$<TARGET_FILE_DIR:${target}>" VERBATIM)
        foreach(plugin coreelements app udp rtp rtpmanager jpeg jpegformat videoconvertscale)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${GSTREAMER_ROOT}/lib/gstreamer-1.0/gst${plugin}.dll"
                    "$<TARGET_FILE_DIR:${target}>/gstreamer-1.0" VERBATIM)
        endforeach()
    endif()
endfunction()

qt_add_executable(pc-application
    src/main.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/VideoWidget.cpp
    src/ui/VideoWidget.h
)

target_link_libraries(pc-application
    PRIVATE
        Qt6::Widgets
        Qt6::Network
        video-receiver
)

deploy_runtime(pc-application)

qt_add_executable(mock-video-sender tools/mock/video/main.cpp)
target_link_libraries(mock-video-sender PRIVATE video-receiver)
deploy_runtime(mock-video-sender)

option(BUILD_TESTING "Build application tests" OFF)
if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(video-integration-test tests/integration/VideoReceiverTest.cpp
        src/ui/MainWindow.cpp src/ui/MainWindow.h src/ui/VideoWidget.cpp src/ui/VideoWidget.h)
    target_link_libraries(video-integration-test PRIVATE video-receiver Qt6::Widgets Qt6::Test)
    deploy_runtime(video-integration-test)
    add_test(NAME video-integration COMMAND video-integration-test -o -,txt)
endif()
