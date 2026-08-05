set(LEGACY_VULKAN_HELPER_SOURCES
    _old/builder.cpp
    _old/builder.h
    _old/vulkan_commandbuffer.cpp
    _old/vulkan_commandbuffer.h
    _old/vulkan_device.cpp
    _old/vulkan_device.h
    _old/vulkan_framebuffer.cpp
    _old/vulkan_framebuffer.h
    _old/vulkan_instance.cpp
    _old/vulkan_instance.h
    _old/vulkan_pipeline.cpp
    _old/vulkan_pipeline.h
    _old/vulkan_queue.cpp
    _old/vulkan_queue.h
    _old/vulkan_renderpass.cpp
    _old/vulkan_renderpass.h
    _old/vulkan_shader.cpp
    _old/vulkan_shader.h
    _old/vulkan_synchronization.cpp
    _old/vulkan_synchronization.h
    _old/vulkan_window.cpp
    _old/vulkan_window.h
)

add_library(vulkan_legacy_old_class STATIC EXCLUDE_FROM_ALL
    ${LEGACY_VULKAN_HELPER_SOURCES}
)

target_include_directories(vulkan_legacy_old_class
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(vulkan_legacy_old_class PUBLIC Vulkan::Vulkan)

add_executable(VulkanSampleLegacy EXCLUDE_FROM_ALL
    main.cpp
    app_sample.cpp
    app_sample.h
    vulkan_sample.cpp
    vulkan_sample.h
)

target_include_directories(VulkanSampleLegacy
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(VulkanSampleLegacy
    PRIVATE
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
        SDL3::SDL3
        glm::glm
        vk-bootstrap::vk-bootstrap
        imgui::imgui
        utility
        vulkan_resource_allocator
        dcl::gltf
        vulkan_legacy_old_class
        callable
        template
        interface
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(VulkanSampleLegacy PRIVATE -Wall)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(vulkan_legacy_old_class PRIVATE /utf-8)
    target_compile_options(VulkanSampleLegacy PRIVATE /W4 /utf-8)
endif()

target_compile_options(VulkanSampleLegacy PRIVATE
    $<$<CONFIG:Debug>:$<$<CXX_COMPILER_ID:GNU,Clang>:-g -O0>>
    $<$<CONFIG:Debug>:$<$<CXX_COMPILER_ID:MSVC>:/Zi /Od>>
    $<$<CONFIG:Release>:$<$<CXX_COMPILER_ID:GNU,Clang>:-O3>>
    $<$<CONFIG:Release>:$<$<CXX_COMPILER_ID:MSVC>:/O2>>
)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}
    PREFIX "legacy"
    FILES
        main.cpp
        app_sample.cpp
        app_sample.h
        vulkan_sample.cpp
        vulkan_sample.h
        ${LEGACY_VULKAN_HELPER_SOURCES}
)
