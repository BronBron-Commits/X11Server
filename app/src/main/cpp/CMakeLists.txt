cmake_minimum_required(VERSION 3.22.1)

project(x11server)

add_library(
        x11server
        SHARED
        native-lib.cpp
        egl_renderer.c
        x11_server.c
        input.c
)

find_library(log-lib log)
find_library(android-lib android)
find_library(egl-lib EGL)
find_library(gles-lib GLESv2)

target_link_libraries(
        x11server
        ${log-lib}
        ${android-lib}
        ${egl-lib}
        ${gles-lib}
)
