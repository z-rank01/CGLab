add_library(cglab_sdl_vulkan_surface STATIC
    sdl_vulkan_surface_adapter.cpp
    sdl_vulkan_surface_adapter.h
    render_graph_driver.cpp
    render_graph_driver.h
)

target_include_directories(cglab_sdl_vulkan_surface PUBLIC ${PROJECT_SOURCE_DIR}/src)

target_link_libraries(cglab_sdl_vulkan_surface
    PUBLIC render_graph::vulkan
    PUBLIC cglab_engine_core
    PRIVATE SDL3::SDL3 cglab_platform_sdl
)

if(MSVC)
    target_compile_options(cglab_sdl_vulkan_surface PRIVATE /W4 /FS /utf-8)
endif()
