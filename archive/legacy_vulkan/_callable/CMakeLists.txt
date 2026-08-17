add_library(callable STATIC
    callable.h
    callable.cpp
)

# 设置头文件包含目录
target_include_directories(callable
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..  # 引用其他模块目录下的头文件
)