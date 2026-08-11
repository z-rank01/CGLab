add_library(cglab_render_graph_vulkan STATIC
    vulkan_backend.cpp
    vulkan_context.cpp
    vulkan_swapchain.cpp
    vulkan_frame_resources.cpp
    vulkan_geometry_store.cpp
    vulkan_gpu_scene.cpp
    vulkan_graph_executor.cpp
    vulkan_backend_internal.h
    vulkan_backend.h
    create_vulkan_backend.cpp
    create_vulkan_backend.h
    render_program.h
    ${PROJECT_SOURCE_DIR}/src/swapchain_image_state.h
)

target_include_directories(cglab_render_graph_vulkan
    PUBLIC ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(cglab_render_graph_vulkan
    PUBLIC
        cglab_engine_core
    PRIVATE
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
        glm::glm
        utility
        render_graph
        cglab_sdl_vulkan_surface
)

if(MSVC)
    target_compile_options(cglab_render_graph_vulkan PRIVATE /W4 /FS /utf-8)
endif()

# Keep the library in the build root for simple target inspection.
set_target_properties(cglab_render_graph_vulkan PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
