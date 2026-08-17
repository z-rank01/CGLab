add_library(vulkan_old_class STATIC
    builder.h
    builder.cpp
    vulkan_synchronization.cpp
    vulkan_synchronization.h
    vulkan_instance.cpp
    vulkan_instance.h
    vulkan_device.cpp
    vulkan_device.h
    vulkan_queue.h
    vulkan_queue.cpp
    vulkan_window.h
    vulkan_window.cpp
    vulkan_shader.cpp
    vulkan_shader.h
    vulkan_pipeline.cpp
    vulkan_pipeline.h
    vulkan_renderpass.cpp
    vulkan_renderpass.h
    vulkan_framebuffer.cpp
    vulkan_framebuffer.h
    vulkan_commandbuffer.cpp
    vulkan_commandbuffer.h
)

# 设置头文件包含目录
target_include_directories(vulkan_old_class
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..  # 引用其他模块目录下的头文件
)

# 链接 Vulkan 和 VulkanMemoryAllocator
target_link_libraries(vulkan_old_class
    PUBLIC
        Vulkan::Vulkan
)