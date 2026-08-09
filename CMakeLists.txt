cmake_minimum_required(VERSION 3.20)

# 设置 vcpkg 工具链文件（如果未通过命令行指定）
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
      CACHE STRING "Vcpkg toolchain file")
endif()

# 设置 vcpkg 清单模式
set(VCPKG_MANIFEST_MODE ON)

project(VulkanSample LANGUAGES CXX)

include(CTest)

# 启用 C++20 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # 避免使用编译器特定扩展

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
add_subdirectory(src/engine)
add_subdirectory(src/_interface)
add_subdirectory(src/control_plane)
# add_subdirectory(src/render_graph)

# third_party/digital-content-loader

# The upstream project can optionally build sample/test executables which require their own entry points.
# Disable them by default to keep the main build green; opt-in via this option if needed.
option(CGLAB_BUILD_DCL_SAMPLES "Build digital-content-loader sample executables" OFF)
set(DCL_BUILD_SAMPLES ${CGLAB_BUILD_DCL_SAMPLES} CACHE BOOL "Build sample/test executables (digital-content-loader)" FORCE)

# Exclude third-party targets (notably sample executables) from the default ALL build.
# The required library targets will still be built because VulkanSample links against them.
add_subdirectory(third_party/digital-content-loader EXCLUDE_FROM_ALL)
add_subdirectory(src/asset)

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
    src/smoke_scene.cpp
)

# 设置头文件
set(HEADERS
    src/application_options.h
    src/smoke_scene.h
    src/swapchain_image_state.h
)

add_library(cglab_vulkan_renderer STATIC
    src/renderer/vulkan/vulkan_renderer.cpp
    src/renderer/vulkan/vulkan_renderer.h
    src/swapchain_image_state.h
    src/renderer/vulkan/create_vulkan_renderer.cpp
    src/renderer/vulkan/create_vulkan_renderer.h
    src/renderer/vulkan/render_program.h
)

target_include_directories(cglab_vulkan_renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(cglab_vulkan_renderer
  PUBLIC cglab_engine_core
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

add_library(cglab_framework_runtime STATIC
    src/framework/engine_runtime.cpp
    src/framework/engine_runtime.h
    src/framework/asset_service.h
    src/framework/runtime_config.h
)

target_include_directories(cglab_framework_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(cglab_framework_runtime
  PUBLIC cglab_engine_core
  PRIVATE
    cglab_platform_sdl
    cglab_asset_gltf
    control_plane
    utility
    $<$<PLATFORM_ID:Windows>:bcrypt>
)

add_library(cglab_application_runner STATIC
    src/apps/application_runner.cpp
    src/apps/application_runner.h
    src/application_options.cpp
    src/application_options.h
)
target_include_directories(cglab_application_runner PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(cglab_application_runner PUBLIC cglab_framework_runtime PRIVATE utility)

if(MSVC)
    target_compile_options(cglab_vulkan_renderer PRIVATE /W4 /FS /utf-8)
    target_compile_options(cglab_framework_runtime PRIVATE /W4 /FS /utf-8)
    target_compile_options(cglab_application_runner PRIVATE /W4 /FS /utf-8)
endif()

# 添加可执行文件
add_executable(VulkanSample ${SOURCES} ${HEADERS})

add_executable(TriangleSample
    src/apps/triangle_sample.cpp
)
target_include_directories(TriangleSample PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(TriangleSample PRIVATE cglab_application_runner cglab_vulkan_renderer)
target_compile_definitions(TriangleSample PRIVATE CGLAB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
    target_compile_options(TriangleSample PRIVATE /W4 /FS /utf-8)
endif()

if(CGLAB_BUILD_LEGACY_VULKAN_SAMPLE)
    add_subdirectory(src/legacy_vulkan_sample EXCLUDE_FROM_ALL)
endif()

# 链接库
target_link_libraries(VulkanSample
  PRIVATE
    glm::glm
    dcl::gltf
    cglab_application_runner
    cglab_vulkan_renderer
    utility
    $<$<PLATFORM_ID:Windows>:bcrypt>   # mbedtls (ixwebsocket) 需要 BCryptGenRandom
)

# 包含目录
target_include_directories(VulkanSample
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/build/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/include
    ${Vulkan_INCLUDE_DIRS}
)

target_compile_definitions(VulkanSample PRIVATE CGLAB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

if(BUILD_TESTING)
    add_executable(cglab_application_options_tests
        src/application_options.cpp
        src/application_options.h
        src/unit_test/application_options_test.cpp
    )
    target_include_directories(cglab_application_options_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(cglab_application_options_tests PRIVATE /FS /utf-8)
    endif()
    add_test(NAME cglab.application_options COMMAND cglab_application_options_tests)

    add_executable(cglab_smoke_scene_tests
        src/smoke_scene.cpp
        src/smoke_scene.h
        src/swapchain_image_state.h
        src/unit_test/smoke_scene_test.cpp
    )
    target_include_directories(cglab_smoke_scene_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(cglab_smoke_scene_tests PRIVATE dcl::gltf glm::glm)
    if(MSVC)
        target_compile_options(cglab_smoke_scene_tests PRIVATE /FS /utf-8)
    endif()
    add_test(NAME cglab.smoke_scene COMMAND cglab_smoke_scene_tests)

    add_executable(cglab_camera_input_tests
        src/unit_test/camera_input_test.cpp
    )
    target_include_directories(cglab_camera_input_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(cglab_camera_input_tests PRIVATE cglab_platform_sdl)
    if(MSVC)
        target_compile_options(cglab_camera_input_tests PRIVATE /FS /utf-8)
    endif()
    add_test(NAME cglab.camera_input COMMAND cglab_camera_input_tests)

    add_executable(cglab_camera_rig_tests
        src/unit_test/camera_rig_test.cpp
    )
    target_include_directories(cglab_camera_rig_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(cglab_camera_rig_tests PRIVATE cglab_engine_core)
    if(MSVC)
        target_compile_options(cglab_camera_rig_tests PRIVATE /FS /utf-8)
    endif()
    add_test(NAME cglab.camera_rig COMMAND cglab_camera_rig_tests)

    add_executable(cglab_scene_registry_tests
        src/unit_test/scene_registry_test.cpp
    )
    target_include_directories(cglab_scene_registry_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(cglab_scene_registry_tests PRIVATE glm::glm)
    if(MSVC)
        target_compile_options(cglab_scene_registry_tests PRIVATE /FS /utf-8)
    endif()
    add_test(NAME cglab.scene_registry COMMAND cglab_scene_registry_tests)

    add_executable(cglab_control_plane_protocol_tests
        src/control_plane/json_rpc.cpp
        src/unit_test/control_plane_protocol_test.cpp
    )
    target_include_directories(cglab_control_plane_protocol_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/control_plane
    )
    find_package(nlohmann_json CONFIG REQUIRED)
    target_link_libraries(cglab_control_plane_protocol_tests PRIVATE nlohmann_json::nlohmann_json)
    if(MSVC)
        target_compile_options(cglab_control_plane_protocol_tests PRIVATE /FS /utf-8)
    endif()
    add_test(NAME cglab.control_plane_protocol COMMAND cglab_control_plane_protocol_tests)

    add_executable(cglab_engine_runtime_tests
        src/unit_test/engine_runtime_test.cpp
    )
    target_include_directories(cglab_engine_runtime_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(cglab_engine_runtime_tests PRIVATE cglab_framework_runtime)
    if(MSVC)
        target_compile_options(cglab_engine_runtime_tests PRIVATE /FS /utf-8)
    endif()
    add_test(NAME cglab.engine_runtime COMMAND cglab_engine_runtime_tests)

    add_executable(cglab_public_headers_tests src/unit_test/public_headers_test.cpp)
    target_include_directories(cglab_public_headers_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(cglab_public_headers_tests PRIVATE cglab_framework_runtime)
    if(MSVC)
        target_compile_options(cglab_public_headers_tests PRIVATE /FS /utf-8)
    endif()
    add_test(NAME cglab.public_headers COMMAND cglab_public_headers_tests)

    add_executable(cglab_asset_gltf_tests src/unit_test/asset_gltf_test.cpp)
    target_include_directories(cglab_asset_gltf_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(cglab_asset_gltf_tests PRIVATE cglab_asset_gltf)
    target_compile_definitions(cglab_asset_gltf_tests PRIVATE CGLAB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(cglab_asset_gltf_tests PRIVATE /FS /utf-8)
    endif()
    add_test(NAME cglab.asset_gltf COMMAND cglab_asset_gltf_tests)

    add_executable(cglab_asset_service_tests src/unit_test/asset_service_test.cpp)
    target_include_directories(cglab_asset_service_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(cglab_asset_service_tests PRIVATE cglab_asset_gltf)
    target_compile_definitions(cglab_asset_service_tests PRIVATE CGLAB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(cglab_asset_service_tests PRIVATE /FS /utf-8)
    endif()
    add_test(NAME cglab.asset_service COMMAND cglab_asset_service_tests)
endif()

# 设置特定于编译器的警告选项
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Clang 可能的 ID 包括 "Clang", "AppleClang" 等
    target_compile_options(VulkanSample PRIVATE -Wall)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(VulkanSample PRIVATE /W4 /FS /utf-8)
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
