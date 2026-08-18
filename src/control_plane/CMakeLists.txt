cmake_minimum_required(VERSION 3.20)

project(control_plane LANGUAGES CXX)

add_library(control_plane STATIC
    json_rpc.h
    json_rpc.cpp
    web_static.h
    control_plane_server.h
    control_plane_server.cpp
)

find_package(ixwebsocket CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

target_link_libraries(control_plane
    PUBLIC
        nlohmann_json::nlohmann_json
    PRIVATE
        ixwebsocket::ixwebsocket
        utility
)

target_include_directories(control_plane
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

if(MSVC)
    target_compile_options(control_plane PRIVATE /W4 /FS /utf-8)
endif()
