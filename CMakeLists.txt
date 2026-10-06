cmake_minimum_required(VERSION 3.25)

project(pc-application LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_standard_project_setup()

qt_add_executable(pc-application
    src/main.cpp
)

target_link_libraries(pc-application
    PRIVATE
        Qt6::Widgets
        Qt6::Network
)

if(WIN32)
    add_custom_command(TARGET pc-application POST_BUILD
        COMMAND Qt6::windeployqt
            --$<IF:$<CONFIG:Debug>,debug,release>
            --no-translations
            "$<TARGET_FILE:pc-application>"
        COMMENT "Deploying Qt runtime dependencies"
        VERBATIM
    )
endif()
