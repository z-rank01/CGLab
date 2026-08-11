add_library(template STATIC
    common.h
    common.hpp
    common.cpp
)

# 设置头文件包含目录
target_include_directories(template
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..  # 引用其他模块目录下的头文件
)

# 链接库
target_link_libraries(template
    PUBLIC
        Vulkan::Vulkan
)