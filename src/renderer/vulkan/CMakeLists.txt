add_library(cglab_vulkan_renderer STATIC
    vulkan_renderer.cpp
    vulkan_renderer.h
    create_vulkan_renderer.cpp
    create_vulkan_renderer.h
    render_program.h
    ${PROJECT_SOURCE_DIR}/src/swapchain_image_state.h
)

target_include_directories(cglab_vulkan_renderer
    PUBLIC ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(cglab_vulkan_renderer
    PUBLIC
        cglab_engine_core
    PRIVATE
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
        SDL3::SDL3
        glm::glm
        vk-bootstrap::vk-bootstrap
        utility
        vulkan_resource_allocator
        vulkan_old_class
        callable
        template
        render_graph
        dcl::gltf
)

if(MSVC)
    target_compile_options(cglab_vulkan_renderer PRIVATE /W4 /FS /utf-8)
endif()

# Keep the historical build/cglab_vulkan_renderer.lib location after moving the
# target definition into this subdirectory.
set_target_properties(cglab_vulkan_renderer PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
