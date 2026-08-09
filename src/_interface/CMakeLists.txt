cmake_minimum_required(VERSION 3.20)

project(interface LANGUAGES CXX)

add_library(cglab_platform_sdl STATIC
        sdl_window.cpp
        sdl_window.h
        Window.h
        input.h
        input_router.h
        camera_component.h
        camera_system.h
)

target_link_libraries(cglab_platform_sdl
    PUBLIC
        cglab_engine_core
        SDL3::SDL3
)

target_include_directories(cglab_platform_sdl
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

add_library(interface ALIAS cglab_platform_sdl)
