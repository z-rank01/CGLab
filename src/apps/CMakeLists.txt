# Shared command-line parsing and runtime startup used by all CGLab apps.
add_library(cglab_application_runner STATIC
    application_runner.cpp
    application_runner.h
    ${PROJECT_SOURCE_DIR}/src/application_options.cpp
    ${PROJECT_SOURCE_DIR}/src/application_options.h
)

target_include_directories(cglab_application_runner
    PUBLIC ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(cglab_application_runner
    PUBLIC
        cglab_framework_runtime
    PRIVATE
        utility
)

if(MSVC)
    target_compile_options(cglab_application_runner PRIVATE /W4 /FS /utf-8)
endif()

set_target_properties(cglab_application_runner PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Asset-driven application. The existing src/main.cpp is this target's entry
# point; it is not shared by the dedicated sample executables below.
set(vulkan_sample_sources
    ${PROJECT_SOURCE_DIR}/src/main.cpp
    ${PROJECT_SOURCE_DIR}/src/smoke_scene.cpp
    ${PROJECT_SOURCE_DIR}/src/application_options.h
    ${PROJECT_SOURCE_DIR}/src/smoke_scene.h
    ${PROJECT_SOURCE_DIR}/src/swapchain_image_state.h
)

add_executable(VulkanSample ${vulkan_sample_sources})

target_include_directories(VulkanSample
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(VulkanSample
    PRIVATE
        glm::glm
        dcl::gltf
        cglab_application_runner
        cglab_vulkan_renderer
        utility
        $<$<PLATFORM_ID:Windows>:bcrypt>
)

target_compile_definitions(VulkanSample
    PRIVATE CGLAB_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(VulkanSample PRIVATE -Wall)
elseif(MSVC)
    target_compile_options(VulkanSample PRIVATE /W4 /FS /utf-8)
endif()

target_compile_options(VulkanSample PRIVATE
    $<$<CONFIG:Debug>:$<$<CXX_COMPILER_ID:GNU,Clang>:-g -O0>>
    $<$<CONFIG:Debug>:$<$<CXX_COMPILER_ID:MSVC>:/Zi /Od>>
    $<$<CONFIG:Release>:$<$<CXX_COMPILER_ID:GNU,Clang>:-O3>>
    $<$<CONFIG:Release>:$<$<CXX_COMPILER_ID:MSVC>:/O2>>
)

source_group(
    TREE ${PROJECT_SOURCE_DIR}
    PREFIX ""
    FILES ${vulkan_sample_sources}
)

# Dedicated triangle app with its own main(). Add future standalone apps beside
# this target and select them with `cmake --build build --target <target>`.
add_executable(TriangleSample
    triangle_sample.cpp
)

target_include_directories(TriangleSample
    PRIVATE ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(TriangleSample
    PRIVATE
        cglab_application_runner
        cglab_vulkan_renderer
)

target_compile_definitions(TriangleSample
    PRIVATE CGLAB_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(TriangleSample PRIVATE /W4 /FS /utf-8)
endif()

# Preserve the paths used by existing scripts and documentation.
set_target_properties(VulkanSample TriangleSample PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
