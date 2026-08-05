cmake_minimum_required(VERSION 3.20)
project(VulkanSample LANGUAGES CXX)

include(CTest)

# 启用 C++23 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # 避免使用编译器特定扩展

# 设置 vcpkg 工具链文件（如果未通过命令行指定）
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
      CACHE STRING "Vcpkg toolchain file")
endif()

# 设置 vcpkg 清单模式
set(VCPKG_MANIFEST_MODE ON)

# 查找依赖
find_package(Vulkan REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Stb REQUIRED)

# 添加子目录
add_subdirectory(src/utility)
add_subdirectory(src/_old)
add_subdirectory(src/_callable)
add_subdirectory(src/_vra)
add_subdirectory(src/_templates)
add_subdirectory(src/_interface)
# add_subdirectory(src/render_graph)

# third_party/digital-content-loader

# The upstream project can optionally build sample/test executables which require their own entry points.
# Disable them by default to keep the main build green; opt-in via this option if needed.
option(CGLAB_BUILD_DCL_SAMPLES "Build digital-content-loader sample executables" OFF)
set(DCL_BUILD_SAMPLES ${CGLAB_BUILD_DCL_SAMPLES} CACHE BOOL "Build sample/test executables (digital-content-loader)" FORCE)

# Exclude third-party targets (notably sample executables) from the default ALL build.
# The required library targets will still be built because VulkanSample links against them.
add_subdirectory(third_party/digital-content-loader EXCLUDE_FROM_ALL)

# third_party/render-graph
option(CGLAB_BUILD_RENDER_GRAPH_SAMPLES "Build render-graph sample executables" OFF)
option(CGLAB_BUILD_RENDER_GRAPH_UNIT_TESTS "Build render-graph CTest executable" OFF)
option(CGLAB_RENDER_GRAPH_SHARED "Build render_graph as a shared library (DLL)" OFF)
option(CGLAB_BUILD_LEGACY_VULKAN_SAMPLE "Build the protected pre-render-graph VulkanSample target" OFF)

# Pass options into the submodule.
set(RENDER_GRAPH_BUILD_SAMPLES ${CGLAB_BUILD_RENDER_GRAPH_SAMPLES} CACHE BOOL "Build render-graph sample executables" FORCE)
set(RENDER_GRAPH_BUILD_UNIT_TESTS ${CGLAB_BUILD_RENDER_GRAPH_UNIT_TESTS} CACHE BOOL "Build render-graph CTest executable" FORCE)
set(RENDER_GRAPH_BUILD_SHARED ${CGLAB_RENDER_GRAPH_SHARED} CACHE BOOL "Build render_graph as a shared library (DLL)" FORCE)

if (CGLAB_BUILD_RENDER_GRAPH_SAMPLES OR CGLAB_BUILD_RENDER_GRAPH_UNIT_TESTS)
    # Opt-in samples/tests must be part of the generated solution so their
    # executable targets can be built and their CTest entries can run.
    add_subdirectory(third_party/render-graph)
else()
    add_subdirectory(third_party/render-graph EXCLUDE_FROM_ALL)
endif()

# 设置源文件
set(SOURCES
    src/main.cpp
    src/vulkan_sample.cpp
    src/app_sample.cpp
)

# 设置头文件
set(HEADERS
    src/vulkan_sample.h
    src/app_sample.h
)

# 添加可执行文件
add_executable(VulkanSample ${SOURCES} ${HEADERS})

if(CGLAB_BUILD_LEGACY_VULKAN_SAMPLE)
    add_subdirectory(src/legacy_vulkan_sample EXCLUDE_FROM_ALL)
endif()

# 链接库
target_link_libraries(VulkanSample
  PRIVATE
    Vulkan::Vulkan
    GPUOpen::VulkanMemoryAllocator
    SDL3::SDL3
    glm::glm
    vk-bootstrap::vk-bootstrap
    imgui::imgui
    utility                         # 添加 utility 库
    vulkan_resource_allocator       # 添加 vra 库
        dcl::gltf                       # glTF loader/parser (third_party/digital-content-loader)
    vulkan_old_class                # 添加旧的 Vulkan 类库
    callable                        # 添加可调用库
    template                        # 添加模板库
    render_graph                    # 添加渲染图库
    interface                       # 添加平台库
)

# 包含目录
target_include_directories(VulkanSample
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/build/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/include
    ${Vulkan_INCLUDE_DIRS}
)

# 设置特定于编译器的警告选项
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Clang 可能的 ID 包括 "Clang", "AppleClang" 等
    target_compile_options(VulkanSample PRIVATE -Wall)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(VulkanSample PRIVATE /W4)
endif()

# 使用生成器表达式为 Debug 构建添加调试信息选项
target_compile_options(VulkanSample PRIVATE
    $<$<CONFIG:Debug>:$<$<CXX_COMPILER_ID:GNU,Clang>:-g -O0>>
    $<$<CONFIG:Debug>:$<$<CXX_COMPILER_ID:MSVC>:/Zi /Od>>
    $<$<CONFIG:Release>:$<$<CXX_COMPILER_ID:GNU,Clang>:-O3>>
    $<$<CONFIG:Release>:$<$<CXX_COMPILER_ID:MSVC>:/O2>>
)

# 设置 IDE 中的源文件分组
source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}
    PREFIX ""
    FILES ${SOURCES} ${HEADERS}
)

# 查询 Vulkan SDK 路径
if (Vulkan_FOUND)
    message(STATUS "Vulkan SDK 位置: ${Vulkan_INCLUDE_DIRS}")
    message(STATUS "Vulkan Libraries 位置: ${Vulkan_LIBRARIES}")
    message(STATUS "Vulkan 版本: ${Vulkan_VERSION}")
else()
    message(FATAL_ERROR "Vulkan SDK not found. Please install the Vulkan SDK.")
endif()

# 查询 CXX 版本信息
message(STATUS "C++ 编译器: ${CMAKE_CXX_COMPILER}")
message(STATUS "C++ 编译器 ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "C++ 编译器版本: ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ 标准: ${CMAKE_CXX_STANDARD}")
message(STATUS "C++ 标准是否必需: ${CMAKE_CXX_STANDARD_REQUIRED}")
message(STATUS "C++ 扩展是否启用: ${CMAKE_CXX_EXTENSIONS}")
