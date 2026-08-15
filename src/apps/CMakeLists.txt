# Shared command-line parsing and runtime startup used by all CGLab apps.
add_library(cglab_application_runner STATIC
    application_runner.cpp
    application_runner.h
    application_options.cpp
    application_options.h
)

target_include_directories(cglab_application_runner
    PUBLIC ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(cglab_application_runner
    PUBLIC
        cglab_engine_runtime
    PRIVATE
        cglab_platform_sdl
        utility
)

if(MSVC)
    target_compile_options(cglab_application_runner PRIVATE /W4 /FS /utf-8)
endif()

set_target_properties(cglab_application_runner PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Dedicated sample executables. Both are part of the default ALL build; use
# `cmake --build build --target <target>` when only one app is needed.
add_executable(TriangleSample
    triangle_sample.cpp
    triangle_render_recipe.cpp
    triangle_render_recipe.h
)

add_executable(GltfSponzaSample
    gltf_sponza_sample.cpp
    gltf_render_recipe.cpp
    gltf_render_recipe.h
    debug_view.h
    lights_table.h
)

# CullingSample：GltfSponzaSample 的剔除演示变体，复用同一 recipe。
add_executable(CullingSample
    culling_sample.cpp
    gltf_render_recipe.cpp
    gltf_render_recipe.h
    debug_view.h
    lights_table.h
)

# ShadowSample：小物件阴影展示（接影地面 + 斜射太阳光），复用同一 recipe。
add_executable(ShadowSample
    shadow_sample.cpp
    gltf_render_recipe.cpp
    gltf_render_recipe.h
    debug_view.h
    lights_table.h
    sun_light.h
)

foreach(cglab_app IN ITEMS TriangleSample GltfSponzaSample CullingSample ShadowSample)
    target_include_directories(${cglab_app}
        PRIVATE ${PROJECT_SOURCE_DIR}/src
    )

    target_link_libraries(${cglab_app}
        PRIVATE
            cglab_application_runner
    )

    target_compile_definitions(${cglab_app}
        PRIVATE CGLAB_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    )

    if(MSVC)
        target_compile_options(${cglab_app} PRIVATE /W4 /FS /utf-8)
    endif()
endforeach()

target_link_libraries(TriangleSample PRIVATE cglab_sdl_vulkan_surface)
target_link_libraries(GltfSponzaSample PRIVATE cglab_sdl_vulkan_surface)
target_link_libraries(CullingSample PRIVATE cglab_sdl_vulkan_surface)
target_link_libraries(ShadowSample PRIVATE cglab_sdl_vulkan_surface)

# Preserve the paths used by existing scripts and documentation.
set_target_properties(TriangleSample GltfSponzaSample CullingSample ShadowSample PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
