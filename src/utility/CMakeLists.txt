# 添加库：vulkan_logger
add_library(utility STATIC
    logger.h
    logger.cpp
    config_reader.h
)

# 设置头文件包含目录
target_include_directories(utility
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..  # 引用其他目录下的头文件 
)

# 链接 Vulkan
target_link_libraries(utility
    PRIVATE
        Vulkan::Vulkan
)
